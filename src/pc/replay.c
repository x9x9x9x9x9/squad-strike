/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Match replays: see replay.h.
 *
 * A replay is the match's setup and, for every simulation tick of it, the
 * four controllers' inputs and the RNG seed the tick started from. Played
 * back, the same setup and the same inputs give the same match, the
 * property netplay already depends on. Storing the seed each tick (it costs
 * four bytes) makes the playback immune to anything that stirs the RNG
 * outside the simulation: the render, the audio thread, the wall clock.
 *
 * Every 30th tick also carries frame_checksum() (inputs, seed, each
 * fighter's position, velocity, percent, stocks and action state), so a
 * playback that drifts from what was recorded is noticed on the spot and
 * says so instead of quietly showing a different match.
 *
 * Online, a tick can run more than once: a rollback re-runs it on the
 * peer's real inputs. So ticks are staged by netplay frame and only the
 * pass no rollback can change any more is kept, as slp.c does.
 *
 * File: SsrHeader, then the frames, zlib-compressed. The header is written
 * when the match ends, so a match cut short by a crash leaves no file. */
#include "compat.h"
#include "pc/replay.h"
#include "pc/net.h"
#include "pc/net_internal.h"
#include "pc/net_match.h"
#include "pc/paths.h"
#include "pc/pc.h"
#include "pc/slp.h"
#include "pc/video_export.h"

#include <aurora/aurora.h>

#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wscalar-storage-order" /* disc-struct unions in lb/types.h */
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmsquad.h>
#include <melee/gm/types.h>
#include <melee/gr/grace.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#pragma GCC diagnostic pop

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>
#include <SDL3/SDL_timer.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

uint32_t frame_checksum(const PADStatus* head); /* net_snapshot.c */
void pc_relaunch_launcher(void);                /* main.c */
void pc_audio_export(bool on);                  /* audio.c */
void pc_audio_export_step(void);
static void export_begin(void);
static void export_end(bool keep);
bool pc_net_deterministic(void); /* net.c */
bool pc_is_ucf_enabled(void);
bool pc_is_free_camera_enabled(void);
bool pc_is_unlock_all_enabled(void);
bool pc_is_squad_strike_enabled(void);
bool pc_is_stage_expansion_enabled(void);
bool pc_is_frozen_stadium_enabled(void);
bool pc_is_results_screen_enabled(void);
bool pc_ace_stage_offered(int stage);
bool pc_sdremix_active(void);
const char* pc_get_net_name(void);

#define SSR_VERSION 2 /* 2: the squad match state (GmSquadLive) */
#define SSR_CK_EVERY 30
/* The setup bytes of StartMeleeRules: everything before its callbacks,
 * which are pointers into this process and mean nothing in another. */
#define RULES_BYTES offsetof(StartMeleeRules, on_unpause_override)

enum {
    FR_CONSUMED = 1, /* the tick read a queue entry: pads follow */
    FR_CHECKSUM = 2, /* a frame_checksum follows the seed */
};

typedef struct SsrHeader {
    char magic[4]; /* "SSRP" */
    uint16_t version;
    uint16_t header_size;
    char app_version[32];
    int64_t created; /* unix seconds, UTC */
    uint8_t mode;    /* gm_GetCurrentGameMode() when recorded */
    uint8_t online;
    uint8_t ace_pick;
    uint8_t rule_flags; /* PC_RULE_* in force */
    uint8_t ucf, free_camera, online_rules, sdremix;
    uint8_t unlock_all;
    uint8_t pad_size; /* sizeof(PADStatus) */
    uint16_t rules_size;
    uint16_t player_size;
    uint16_t players;
    uint64_t unlock;
    uint8_t rules[RULES_BYTES];
    PlayerInitData player[GM_MAX_PLAYERS];
    GmSquadMember squad[GMSQUAD_PORTS][GMSQUAD_EXTRA];
    uint8_t coin_pos[GMSQUAD_PORTS];
    GmSquadLive squad_live; /* the lineups gmSquad_OnEnterVs built */
    char name[32];          /* this machine's player name */
    uint32_t frames;
    uint32_t data_size; /* uncompressed frame bytes */
    uint32_t packed_size;
    int8_t end_stocks[GM_MAX_PLAYERS];
    /* The seed entering the match's setup: fighter and stage creation draw
     * from it before the first tick (CPU timers, the stage's own state). */
    uint32_t start_seed;
    /* Who recorded it, for the stats page: this machine's port + 1 (0 when
     * there is no single one, as with two people on one controller set),
     * and online the opponent's connect code. Zero in files from before
     * they were kept, which read as unknown. */
    uint8_t local_port;
    char opponent[16];
    uint8_t reserved[11];
} __attribute__((packed)) SsrHeader;

/* ---- recording -------------------------------------------------------- */

#define STAGE_RING 512
typedef struct Staged {
    int32_t frame; /* -1: empty */
    uint8_t flags;
    PADStatus pads[4];
    uint32_t seed;
    uint32_t ck;
} Staged;

static Staged s_ring[STAGE_RING];
static bool s_rec;            /* a match is being recorded */
static int32_t s_rec_first;   /* netplay frame of its first tick, -1 before */
static int32_t s_rec_emitted; /* newest frame appended to s_buf */
static int32_t s_rec_staged;  /* newest frame staged */
static uint8_t* s_buf;
static size_t s_len, s_cap;
static uint32_t s_frames;
static SsrHeader s_hdr;

/* ---- playback --------------------------------------------------------- */

static bool s_playing;    /* this process plays a replay */
static bool s_play_match; /* ... and its match is running */
static SsrHeader s_play;
static uint8_t* s_data; /* the frames, uncompressed */
static size_t s_data_len, s_pos;
static uint32_t s_play_frame;
static bool s_diverged;
static PADStatus s_play_pads[4];

static void put(const void* p, size_t n) {
    if (s_len + n > s_cap) {
        size_t cap = s_cap ? s_cap * 2 : 1 << 20;
        while (cap < s_len + n) {
            cap *= 2;
        }
        uint8_t* b = realloc(s_buf, cap);
        if (b == NULL) {
            return;
        }
        s_buf = b;
        s_cap = cap;
    }
    memcpy(s_buf + s_len, p, n);
    s_len += n;
}

static bool recording_wanted(void) {
    if (s_playing) {
        return false;
    }
    const char* e = getenv("MELEE_REPLAYS");
    if (e != NULL) {
        return e[0] != '0';
    }
    extern bool pc_is_replay_saving_enabled(void);
    return pc_is_replay_saving_enabled();
}

static bool recorded_mode(void) {
    switch (gm_GetCurrentGameMode()) {
    case GM_VS:
    case GM_DEBUG_VS:
    case GM_ONLINE:
    case GM_TOURNAMENT:
    case GM_SUPER_SUDDEN_DEATH_VS:
    case GM_INVISIBLE_VS:
    case GM_SLOMO_VS:
    case GM_LIGHTNING_VS:
    case GM_TINY_VS:
    case GM_GIANT_VS:
    case GM_STAMINA_VS:
    case GM_SINGLE_BUTTON_VS:
    case GM_CAMERA_VS:
        return true;
    default:
        return false;
    }
}

static unsigned current_rule_flags(void) {
    unsigned f = 0;
    f |= pc_is_frozen_stadium_enabled() ? PC_RULE_FROZEN_STADIUM : 0;
    f |= pc_is_squad_strike_enabled() ? PC_RULE_SQUAD_STRIKE : 0;
    f |= pc_is_stage_expansion_enabled() ? PC_RULE_STAGE_EXPANSION : 0;
    for (int st = 1; st <= 3; st++) {
        f |= pc_ace_stage_offered(st) ? 1u << (PC_RULE_ACE_STAGE_SHIFT + st - 1) : 0;
    }
    f |= pc_is_results_screen_enabled() ? 0 : PC_RULE_NO_RESULTS;
    return f;
}

void pc_replay_match_start(const StartMeleeData* data) {
    if (data != NULL && getenv("MELEE_REPLAY_TRACE") != NULL) {
        uint32_t h = 2166136261u;
        const uint8_t* b = (const uint8_t*)data;
        for (size_t i = 0; i < RULES_BYTES; i++) {
            h = (h ^ b[i]) * 16777619u;
        }
        uint32_t hp = 2166136261u;
        b = (const uint8_t*)data->players;
        for (size_t i = 0; i < sizeof data->players; i++) {
            hp = (hp ^ b[i]) * 16777619u;
        }
        pc_log_line("replay: match setup stage %u rules %08x players %08x seed %08x",
            data->rules.stkind, h, hp, *HSD_RandSeedPtr);
    }
    if (s_playing) {
        s_play_match = data != NULL;
        s_play_frame = 0;
        s_pos = 0;
        if (s_play_match) {
            *HSD_RandSeedPtr = s_play.start_seed;
            export_begin();
            pc_log_line("replay: playing %u frames", s_play.frames);
        }
        return;
    }
    s_rec = false;
    if (data == NULL || !recorded_mode() || !recording_wanted()) {
        return;
    }
    memset(&s_hdr, 0, sizeof s_hdr);
    memcpy(s_hdr.magic, "SSRP", 4);
    s_hdr.version = SSR_VERSION;
    s_hdr.header_size = sizeof s_hdr;
    SDL_strlcpy(s_hdr.app_version, pc_app_version(), sizeof s_hdr.app_version);
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    s_hdr.created = now / SDL_NS_PER_SECOND;
    s_hdr.mode = gm_GetCurrentGameMode();
    s_hdr.online = pc_net_deterministic() && s_hdr.mode == GM_ONLINE;
    s_hdr.ace_pick = (uint8_t)grAce_Pick;
    s_hdr.rule_flags = (uint8_t)current_rule_flags();
    s_hdr.ucf = pc_is_ucf_enabled();
    s_hdr.free_camera = pc_is_free_camera_enabled();
    s_hdr.online_rules = pc_online_ruleset();
    s_hdr.sdremix = pc_sdremix_active();
    s_hdr.unlock_all = pc_is_unlock_all_enabled();
    s_hdr.pad_size = sizeof(PADStatus);
    s_hdr.rules_size = RULES_BYTES;
    s_hdr.player_size = sizeof(PlayerInitData);
    s_hdr.players = GM_MAX_PLAYERS;
    s_hdr.unlock = pc_unlock_state_get();
    memcpy(s_hdr.rules, &data->rules, RULES_BYTES);
    memcpy(s_hdr.player, data->players, sizeof s_hdr.player);
    gmSquad_SaveLive(&s_hdr.squad_live);
    if (gmSquad_ModeAllowed()) {
        memcpy(s_hdr.squad, gmSquad_rows, sizeof s_hdr.squad);
        memcpy(s_hdr.coin_pos, gmSquad_coin_pos, sizeof s_hdr.coin_pos);
    } else {
        memset(s_hdr.squad, GMSQUAD_NONE, sizeof s_hdr.squad);
        memset(s_hdr.coin_pos, GMSQUAD_NONE, sizeof s_hdr.coin_pos);
    }
    SDL_strlcpy(s_hdr.name, pc_get_net_name(), sizeof s_hdr.name);
    s_hdr.start_seed = *HSD_RandSeedPtr;
    s_hdr.local_port = 0;
    memset(s_hdr.opponent, 0, sizeof s_hdr.opponent);
    if (pc_net_active()) {
        const int port = pc_net_local_player();
        s_hdr.local_port = port >= 0 && port < 4 ? (uint8_t)(port + 1) : 0;
        const char* code = pc_net_match_opponent_code();
        SDL_strlcpy(s_hdr.opponent, code != NULL ? code : "", sizeof s_hdr.opponent);
    } else {
        /* Offline the player is the one human; with two or more there is no
         * telling which one is this machine's owner. */
        int humans = 0;
        for (int i = 0; i < 4; i++) {
            if (data->players[i].slot_type == Gm_PKind_Human) {
                humans++;
                s_hdr.local_port = (uint8_t)(i + 1);
            }
        }
        if (humans != 1) {
            s_hdr.local_port = 0;
        }
    }

    for (int i = 0; i < STAGE_RING; i++) {
        s_ring[i].frame = -1;
    }
    s_len = 0;
    s_frames = 0;
    s_rec_first = -1;
    s_rec_emitted = -1;
    s_rec_staged = -1;
    s_rec = true;
}

static void emit_upto(int32_t upto) {
    if (upto > s_rec_staged) {
        upto = s_rec_staged;
    }
    for (int32_t f = s_rec_emitted + 1; f <= upto; f++) {
        const Staged* s = &s_ring[f & (STAGE_RING - 1)];
        if (s->frame != f) {
            /* Aged out unconfirmed (a stall longer than the ring): the
             * replay cannot be completed truthfully. */
            pc_log_line("replay: frame %d lost, this match will not be saved", f);
            s_rec = false;
            return;
        }
        uint8_t flags = s->flags;
        if ((s_frames % SSR_CK_EVERY) == 0) {
            flags |= FR_CHECKSUM;
        }
        put(&flags, 1);
        if (flags & FR_CONSUMED) {
            put(s->pads, sizeof s->pads);
        }
        put(&s->seed, 4);
        if (flags & FR_CHECKSUM) {
            put(&s->ck, 4);
        }
        s_frames++;
        s_rec_emitted = f;
    }
}

/* MELEE_REPLAY_TRACE=file: one line per tick (recording or playback) with
 * the seed and every fighter's position, to diff two runs. Debug only. */
static void trace(const char* what, uint32_t idx, const PADStatus* pads) {
    static FILE* f;
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("MELEE_REPLAY_TRACE");
        on = e != NULL && e[0] != '\0';
        if (on) {
            f = fopen(e, "w");
        }
    }
    if (!on || f == NULL) {
        return;
    }
    fprintf(f, "%s %u seed %08x pads %04x %04x %04x %04x", what, idx, *HSD_RandSeedPtr,
        pads[0].button, pads[1].button, pads[2].button, pads[3].button);
    for (int slot = 0; slot < 4; slot++) {
        HSD_GObj* g = Player_GetEntity(slot);
        if (g == NULL || g->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        const Fighter* fp = GET_FIGHTER(g);
        fprintf(f, " | p%d %.4f,%.4f m%d %.2f v%.4f,%.4f kb%.4f,%.4f f%.0f a%.3f", slot,
            fp->cur_pos.x, fp->cur_pos.y, (int)fp->motion_id, fp->dmg.x1830_percent, fp->self_vel.x,
            fp->self_vel.y, fp->x8c_kb_vel.x, fp->x8c_kb_vel.y, fp->facing_dir, fp->cur_anim_frame);
    }
    fputc('\n', f);
    fflush(f);
}

static const PADStatus* record_tick(const PADStatus* q) {
    int32_t f = pc_net_sim_frame();
    if (s_rec_first < 0) {
        s_rec_first = f;
        s_rec_emitted = f - 1;
    }
    if (f < s_rec_first || f <= s_rec_emitted) {
        return NULL; /* a re-run of a frame already final */
    }
    Staged* s = &s_ring[f & (STAGE_RING - 1)];
    s->frame = f;
    s->flags = q != NULL ? FR_CONSUMED : 0;
    if (q != NULL) {
        memcpy(s->pads, q, sizeof s->pads);
    } else {
        memset(s->pads, 0, sizeof s->pads);
    }
    s->seed = *HSD_RandSeedPtr;
    s->ck = frame_checksum(s->pads);
    trace("rec", (uint32_t)(f - s_rec_first), s->pads);
    if (f > s_rec_staged) {
        s_rec_staged = f;
    }
    /* Online only the frames no rollback can reach are final; offline
     * every frame before this one is. */
    int32_t confirmed = pc_net_confirmed_frame();
    emit_upto(confirmed != INT32_MIN ? confirmed : f - 1);
    return NULL;
}

static char* replay_dir(void) {
    char* pref = pc_get_pref_path();
    if (pref == NULL) {
        return NULL;
    }
    size_t n = strlen(pref) + 16;
    char* dir = malloc(n);
    if (dir != NULL) {
        snprintf(dir, n, "%sreplays", pref);
        SDL_CreateDirectory(dir);
    }
    SDL_free(pref);
    return dir;
}

void pc_replay_match_end(void) {
    if (s_playing) {
        if (s_play_match) {
            pc_log_line("replay: finished at frame %u of %u%s", s_play_frame, s_play.frames,
                s_diverged ? " (out of sync)" : "");
        }
        s_play_match = false;
        export_end(true);
        /* The match is over: back to the launcher's replay list. */
        if (getenv("MELEE_REPLAY_NO_RELAUNCH") == NULL) {
            pc_relaunch_launcher();
        }
        return;
    }
    if (!s_rec) {
        return;
    }
    s_rec = false;
    emit_upto(s_rec_staged);
    if (s_frames < 60) {
        return; /* a match quit during its countdown */
    }
    for (int i = 0; i < GM_MAX_PLAYERS; i++) {
        s_hdr.end_stocks[i] = (int8_t)Player_GetStocks(i);
    }
    uLongf packed_cap = compressBound(s_len);
    uint8_t* packed = malloc(packed_cap);
    if (packed == NULL || compress2(packed, &packed_cap, s_buf, s_len, 9) != Z_OK) {
        free(packed);
        pc_log_line("replay: could not compress %zu bytes", s_len);
        return;
    }
    s_hdr.frames = s_frames;
    s_hdr.data_size = (uint32_t)s_len;
    s_hdr.packed_size = (uint32_t)packed_cap;

    char* dir = replay_dir();
    if (dir == NULL) {
        free(packed);
        return;
    }
    SDL_Time now = (SDL_Time)s_hdr.created * SDL_NS_PER_SECOND;
    SDL_DateTime dt;
    SDL_TimeToDateTime(now, &dt, true);
    char path[1024];
    snprintf(path, sizeof path, "%s/Game_%04d%02d%02d_%02d%02d%02d.ssr", dir, dt.year, dt.month,
        dt.day, dt.hour, dt.minute, dt.second);
    free(dir);
    for (int n = 2; n < 100; n++) {
        FILE* exists = fopen(path, "rb");
        if (exists == NULL) {
            break;
        }
        fclose(exists);
        snprintf(path + strlen(path) - 4, 16, "_%d.ssr", n);
    }
    FILE* f = fopen(path, "wb");
    bool ok = f != NULL && fwrite(&s_hdr, sizeof s_hdr, 1, f) == 1 &&
              fwrite(packed, packed_cap, 1, f) == 1;
    if (f != NULL) {
        ok = fclose(f) == 0 && ok;
    }
    free(packed);
    pc_log_line("replay: %s %s (%u frames, %zu bytes)", ok ? "saved" : "could not save", path,
        s_frames, (size_t)(sizeof s_hdr + packed_cap));
}

/* ---- playback --------------------------------------------------------- */

static bool load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        pc_log_line("replay: cannot open %s", path);
        return false;
    }
    bool ok = fread(&s_play, sizeof s_play, 1, f) == 1 && memcmp(s_play.magic, "SSRP", 4) == 0;
    if (ok &&
        (s_play.version != SSR_VERSION || s_play.header_size != sizeof s_play ||
            s_play.rules_size != RULES_BYTES || s_play.player_size != sizeof(PlayerInitData) ||
            s_play.players != GM_MAX_PLAYERS || s_play.pad_size != sizeof(PADStatus)))
    {
        pc_log_line("replay: %s was made by a different version (%s)", path, s_play.app_version);
        ok = false;
    }
    uint8_t* packed = NULL;
    if (ok) {
        packed = malloc(s_play.packed_size);
        s_data = malloc(s_play.data_size ? s_play.data_size : 1);
        uLongf n = s_play.data_size;
        ok = packed != NULL && s_data != NULL && fread(packed, s_play.packed_size, 1, f) == 1 &&
             uncompress(s_data, &n, packed, s_play.packed_size) == Z_OK && n == s_play.data_size;
        s_data_len = n;
    }
    free(packed);
    fclose(f);
    if (!ok) {
        pc_log_line("replay: %s is not a readable replay", path);
        return false;
    }
    if (strcmp(s_play.app_version, pc_app_version()) != 0) {
        pc_log_line("replay: recorded by %s, playing on %s", s_play.app_version, pc_app_version());
    }
    pc_log_line("replay: %s, %u frames, mode %u%s%s", path, s_play.frames, s_play.mode,
        s_play.online ? ", online" : "", s_play.sdremix ? ", SD Remix" : "");
    return true;
}

/* ---- video export ------------------------------------------------------
 * MELEE_REPLAY_EXPORT=<file.mp4> with MELEE_REPLAY: the playback runs one
 * tick per drawn frame, as fast as the machine draws, captures each frame
 * and renders the mix per frame, and writes the video; then the launcher
 * comes back saying where it went. */
static char s_export_path[1024];
/* Handed over in memory, not through the environment: on Windows SDL sets
 * the process environment, which the C runtime's getenv() never sees, so a
 * replay picked in the launcher was not found when the game started. */
static char s_request[1024], s_request_export[1024];
static char s_status[1200]; /* for the launcher, after the relaunch */
static bool s_exporting;    /* the capture and the encoder are running */
static bool s_export_failed;

static void capture_cb(const uint8_t* px, uint32_t w, uint32_t h, uint32_t stride, bool bgra,
    uint64_t index, void* user) {
    (void)user;
    pc_video_frame(px, (int)w, (int)h, (int)stride, bgra, index);
}

static void export_begin(void) {
    if (s_export_path[0] == '\0' || s_exporting) {
        return;
    }
    int height = 1080;
    const char* e = getenv("MELEE_REPLAY_EXPORT_HEIGHT");
    if (e != NULL && atoi(e) >= 240 && atoi(e) <= 2160) {
        height = atoi(e) & ~1;
    }
    int width = (height * 16 / 9) & ~1;
    if (!pc_video_open(s_export_path, width, height, 32000)) {
        s_export_failed = true;
        return;
    }
    if (!aurora_capture_begin((uint32_t)width, (uint32_t)height, capture_cb, NULL)) {
        pc_log_line("replay: this graphics backend cannot capture frames");
        pc_video_close();
        s_export_failed = true;
        return;
    }
    pc_audio_export(true);
    s_exporting = true;
}

static void export_end(bool keep) {
    if (!s_exporting) {
        return;
    }
    s_exporting = false;
    aurora_capture_end();
    pc_audio_export(false);
    bool ok = pc_video_close() && keep;
    if (!ok) {
        remove(s_export_path);
    }
    char msg[1200];
    if (!keep) {
        snprintf(msg, sizeof msg, "Video export cancelled.");
    } else if (ok) {
        snprintf(msg, sizeof msg, "Video saved: %s", s_export_path);
    } else {
        snprintf(msg, sizeof msg, "The video could not be written.");
    }
    SDL_strlcpy(s_status, msg, sizeof s_status);
}

bool pc_replay_exporting(void) {
    return s_playing && s_export_path[0] != '\0';
}

void pc_replay_init(void) {
    const char* path = s_request[0] != '\0' ? s_request : getenv("MELEE_REPLAY");
    if (path != NULL && path[0] != '\0') {
        s_playing = load(path);
        const char* out =
            s_request_export[0] != '\0' ? s_request_export : getenv("MELEE_REPLAY_EXPORT");
        if (s_playing && out != NULL && out[0] != '\0') {
            SDL_strlcpy(s_export_path, out, sizeof s_export_path);
        }
    }
}

const char* pc_replay_status(void) {
    return s_status;
}

bool pc_replay_playing(void) {
    return s_playing;
}

bool pc_replay_match_active(void) {
    return s_rec || s_play_match;
}

bool pc_replay_diverged(void) {
    return s_diverged;
}

void pc_replay_setup_match(StartMeleeData* start) {
    if (!s_playing) {
        return;
    }
    memcpy(&start->rules, s_play.rules, RULES_BYTES);
    memcpy(start->players, s_play.player, sizeof s_play.player);
    gmSquad_Restore(s_play.squad, s_play.coin_pos);
    gmSquad_RestoreLive(&s_play.squad_live);
    grAce_Pick = s_play.ace_pick;
    pc_unlock_state_set(s_play.unlock);
}

static bool s_finished; /* the playback ran out of recorded frames */

static const PADStatus* play_tick(bool* skip) {
    if (s_pos >= s_data_len) {
        s_finished = true;
        memset(s_play_pads, 0, sizeof s_play_pads);
        return s_play_pads; /* past the end: nobody touches anything */
    }
    uint8_t flags = s_data[s_pos++];
    if (flags & FR_CONSUMED) {
        memcpy(s_play_pads, s_data + s_pos, sizeof s_play_pads);
        s_pos += sizeof s_play_pads;
    }
    uint32_t seed, ck = 0;
    memcpy(&seed, s_data + s_pos, 4);
    s_pos += 4;
    if (flags & FR_CHECKSUM) {
        memcpy(&ck, s_data + s_pos, 4);
        s_pos += 4;
    }
    *HSD_RandSeedPtr = seed;
    {
        PADStatus tp[4];
        if (flags & FR_CONSUMED) {
            memcpy(tp, s_play_pads, sizeof tp);
        } else {
            memset(tp, 0, sizeof tp);
        }
        trace("rec", s_play_frame, tp);
    }
    if ((flags & FR_CHECKSUM) && !s_diverged) {
        PADStatus pads[4];
        if (flags & FR_CONSUMED) {
            memcpy(pads, s_play_pads, sizeof pads);
        } else {
            memset(pads, 0, sizeof pads);
        }
        uint32_t now = frame_checksum(pads);
        if (now != ck) {
            s_diverged = true;
            pc_log_line(
                "replay: OUT OF SYNC at frame %u (recorded %08x, now %08x)", s_play_frame, ck, now);
        }
    }
    s_play_frame++;
    if (!(flags & FR_CONSUMED)) {
        *skip = true;
        return NULL;
    }
    return s_play_pads;
}

const PADStatus* pc_replay_renew(const PADStatus* q, bool* skip) {
    *skip = false;
    if (s_play_match) {
        return play_tick(skip);
    }
    if (s_rec) {
        return record_tick(q);
    }
    return NULL;
}

int pc_replay_mode_override(void) {
    if (!s_play_match) {
        return -1;
    }
    /* GM_ONLINE's own setup ran when the match was recorded; what is left
     * of it in the match plays exactly like VS. */
    return s_play.mode == GM_ONLINE ? GM_VS : s_play.mode;
}

bool pc_replay_rule_flags(unsigned* flags) {
    if (!s_playing) {
        return false;
    }
    *flags = s_play.rule_flags;
    return true;
}

#define REPLAY_FLAG(fn, field)                                                                     \
    bool fn(bool* on) {                                                                            \
        if (!s_playing) {                                                                          \
            return false;                                                                          \
        }                                                                                          \
        *on = s_play.field != 0;                                                                   \
        return true;                                                                               \
    }
REPLAY_FLAG(pc_replay_ucf, ucf)
REPLAY_FLAG(pc_replay_free_camera, free_camera)
REPLAY_FLAG(pc_replay_online_rules, online_rules)
REPLAY_FLAG(pc_replay_sdremix, sdremix)
REPLAY_FLAG(pc_replay_unlock_all, unlock_all)

bool pc_sim_render_decoupled(void) {
    return pc_net_deterministic() || s_rec || s_play_match;
}

bool pc_online_ruleset(void) {
    bool on;
    if (pc_replay_online_rules(&on)) {
        return on;
    }
    return pc_net_deterministic();
}

/* ---- rewind -------------------------------------------------------------
 * Every 10 s of playback the whole game state is snapshotted (the netplay
 * rollback's own snapshot, net_snapshot.c) beside the read position. Down on
 * the D-pad jumps back to the newest one at least half a second behind,
 * so pressing it again keeps going back; the oldest go first past 30. */
#define CK_EVERY 600
#define CK_MAX 30
typedef struct Checkpoint {
    Snapshot snap;
    uint32_t frame;
    size_t pos;
} Checkpoint;
static Checkpoint s_ck[CK_MAX];
static int s_ck_count;

static void checkpoint_take(void) {
    if ((s_play_frame % CK_EVERY) != 0 ||
        (s_ck_count > 0 && s_ck[s_ck_count - 1].frame >= s_play_frame))
    {
        return;
    }
    if (s_ck_count == CK_MAX) {
        /* Keep the start (frame 0, the restart point); drop the next. */
        Snapshot reuse = s_ck[1].snap;
        memmove(&s_ck[1], &s_ck[2], sizeof s_ck[0] * (CK_MAX - 2));
        s_ck[CK_MAX - 1].snap = reuse;
        s_ck_count--;
    }
    Checkpoint* c = &s_ck[s_ck_count];
    if (!snapshot_take(&c->snap, (int32_t)s_play_frame)) {
        return; /* a transfer in flight: the next one */
    }
    c->frame = s_play_frame;
    c->pos = s_pos;
    s_ck_count++;
    if (s_ck_count == 1) {
        pc_log_line("replay: checkpoint of %zu KB", c->snap.used / 1024);
    }
}

static void replay_rewind(void) {
    for (int i = s_ck_count - 1; i >= 0; i--) {
        if (s_ck[i].frame + 30 <= s_play_frame || (i == 0 && s_ck[i].frame < s_play_frame)) {
            const char* why = snapshot_unusable(&s_ck[i].snap);
            if (why != NULL) {
                pc_log_line("replay: cannot rewind (%s)", why);
                return;
            }
            snapshot_restore(&s_ck[i].snap);
            s_play_frame = s_ck[i].frame;
            s_pos = s_ck[i].pos;
            s_finished = false;
            s_ck_count = i + 1; /* the later ones are retaken as it plays on */
            return;
        }
    }
}

/* ---- playback controls ------------------------------------------------ */

static const float k_speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
#define SPEED_NORMAL 2
static int s_speed = SPEED_NORMAL;
static bool s_paused;
static float s_speed_acc;
static uint16_t s_prev_buttons;

/* The controllers' buttons right now: the newest raw sample, every port.
 * The playback feeds the recorded inputs to the game, so the real
 * controllers are free to drive it. */
static uint16_t live_buttons(void) {
    PadLibData* p = &HSD_PadLibData;
    if (p->queue == NULL || p->qnum == 0) {
        return 0;
    }
    int newest = p->qwrite != 0 ? p->qwrite - 1 : p->qnum - 1;
    uint16_t b = 0;
    for (int port = 0; port < 4; port++) {
        const PADStatus* st = &p->queue->stat[newest * 4 + port];
        if (st->err == 0) {
            b |= st->button;
        }
    }
    return b;
}

int pc_replay_tick_budget(int count) {
    if (!s_play_match) {
        return count;
    }
    uint16_t now = live_buttons();
    uint16_t pressed = now & ~s_prev_buttons;
    s_prev_buttons = now;
    bool step = false;
    if (pressed & PAD_BUTTON_B) {
        pc_log_line("replay: left at frame %u", s_play_frame);
        export_end(false);
        pc_relaunch_launcher();
    }
    if (s_exporting) {
        /* One tick per drawn frame, each frame captured, its sound mixed. */
        HSD_PadFlushQueue(HSD_PAD_FLUSH_QUEUE_THROWAWAY);
        pc_audio_export_step();
        aurora_capture_frame();
        return 1;
    }
    if (pressed & PAD_BUTTON_START) {
        s_paused = !s_paused;
    }
    if (pressed & (PAD_TRIGGER_R | PAD_BUTTON_RIGHT)) {
        s_speed = s_speed < (int)(sizeof k_speeds / sizeof k_speeds[0]) - 1 ? s_speed + 1 : s_speed;
    }
    if (pressed & (PAD_TRIGGER_L | PAD_BUTTON_LEFT)) {
        s_speed = s_speed > 0 ? s_speed - 1 : 0;
    }
    if (pressed & (PAD_BUTTON_Y | PAD_BUTTON_UP)) {
        s_speed = SPEED_NORMAL;
    }
    {
        /* MELEE_REPLAY_TEST_REWIND=frame: one rewind there, for tests. */
        static int test_at = -2;
        if (test_at == -2) {
            const char* e = getenv("MELEE_REPLAY_TEST_REWIND");
            test_at = e != NULL ? atoi(e) : -1;
        }
        if (test_at >= 0 && s_play_frame == (uint32_t)test_at) {
            test_at = -1;
            pressed |= PAD_BUTTON_DOWN;
        }
    }
    if (pressed & PAD_BUTTON_DOWN) {
        uint32_t from = s_play_frame;
        replay_rewind();
        pc_log_line("replay: rewound from frame %u to %u", from, s_play_frame);
    }
    checkpoint_take();
    if (s_paused && (pressed & (PAD_BUTTON_A | PAD_TRIGGER_Z))) {
        step = true;
    }
    int n;
    if (s_paused) {
        n = step ? 1 : 0;
    } else if (k_speeds[s_speed] >= 1.0f) {
        n = (int)(count * k_speeds[s_speed]);
    } else {
        s_speed_acc += count * k_speeds[s_speed];
        n = (int)s_speed_acc;
        s_speed_acc -= n;
    }
    if (n < count) {
        /* The samples this frame does not run: dropped, so the next frame
         * still waits for the pad clock instead of spinning on them. */
        HSD_PadFlushQueue(n == 0 ? HSD_PAD_FLUSH_QUEUE_THROWAWAY : HSD_PAD_FLUSH_QUEUE_LEAVE1);
        if (n > 1) {
            n = 1;
        }
    }
    return n;
}

bool pc_replay_hud(char* buf, int len) {
    if (!s_play_match) {
        return false;
    }
    unsigned t = s_play_frame / 60, total = s_play.frames / 60;
    if (s_exporting) {
        snprintf(buf, len, "EXPORTING VIDEO  %u:%02u / %u:%02u  (%u%%)  \xc2\xb7  B cancel", t / 60,
            t % 60, total / 60, total % 60, s_play.frames ? s_play_frame * 100 / s_play.frames : 0);
        return true;
    }
    char speed[16];
    float sp = k_speeds[s_speed];
    if (sp >= 1.0f) {
        snprintf(speed, sizeof speed, "%dx", (int)sp);
    } else {
        snprintf(speed, sizeof speed, "%sx", sp == 0.5f ? "1/2" : "1/4");
    }
    snprintf(buf, len,
        "REPLAY  %u:%02u / %u:%02u  \xc2\xb7  %s%s  \xc2\xb7  Start pause  L/R speed  D-pad down "
        "rewind%s  B exit",
        t / 60, t % 60, total / 60, total % 60, s_paused ? "PAUSED" : speed,
        s_diverged ? "  \xc2\xb7  OUT OF SYNC" : "", s_paused ? "  A step" : "");
    return true;
}

/* ---- the launcher's list ---------------------------------------------- */

const char* pc_replay_folder(void) {
    static char buf[1024];
    if (buf[0] == '\0') {
        char* dir = replay_dir();
        if (dir != NULL) {
            SDL_strlcpy(buf, dir, sizeof buf);
            free(dir);
        }
    }
    return buf;
}

void pc_replay_request(const char* path, const char* export_to) {
    SDL_strlcpy(s_request, path, sizeof s_request);
    SDL_strlcpy(s_request_export, export_to != NULL ? export_to : "", sizeof s_request_export);
}

static bool info_from(const char* path, const char* file, PcReplayInfo* out) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    SsrHeader h;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && memcmp(h.magic, "SSRP", 4) == 0;
    fseek(f, 0, SEEK_END);
    int64_t size = (int64_t)ftell(f);  // NOLINT: ftell's own type
    fclose(f);
    if (!ok) {
        return false;
    }
    memset(out, 0, sizeof *out);
    SDL_strlcpy(out->path, path, sizeof out->path);
    SDL_strlcpy(out->file, file, sizeof out->file);
    out->playable = h.version == SSR_VERSION && h.header_size == sizeof h &&
                    h.rules_size == RULES_BYTES && h.player_size == sizeof(PlayerInitData) &&
                    h.players == GM_MAX_PLAYERS && h.pad_size == sizeof(PADStatus);
    out->created = h.created;
    out->bytes = (uint32_t)size;
    SDL_strlcpy(out->name, h.name, sizeof out->name);
    SDL_strlcpy(out->version, h.app_version, sizeof out->version);
    out->local_port = h.local_port >= 1 && h.local_port <= 4 ? h.local_port - 1 : -1;
    SDL_strlcpy(out->opponent, h.opponent, sizeof out->opponent);
    if (!out->playable) {
        return true; /* listed, but its layout is not this build's */
    }
    out->frames = h.frames;
    out->mode = h.mode;
    out->online = h.online;
    out->sdremix = h.sdremix;
    out->ace_pick = h.ace_pick;
    StartMeleeRules rules;
    memset(&rules, 0, sizeof rules);
    memcpy(&rules, h.rules, RULES_BYTES);
    out->stkind = rules.stkind;
    out->is_teams = rules.is_teams;
    out->stock_match = rules.is_stock;
    for (int i = 0; i < 4; i++) {
        const PlayerInitData* pl = &h.player[i];
        PcReplayPlayer* rp = &out->players[i];
        bool present = pl->slot_type != Gm_PKind_NA && pl->slot_type != Gm_PKind_Demo;
        rp->ckind = present ? (int8_t)pl->ckind : -1;
        rp->color = pl->color;
        rp->cpu = pl->slot_type == Gm_PKind_Cpu;
        rp->end_stocks = h.end_stocks[i];
        rp->team = pl->team;
        /* A squad: its lineup in fighting order, as the match ran it. */
        const GmSquadLive* sq = &h.squad_live;
        for (int k = 0; k < 3; k++) {
            rp->squad[k] = present && sq->live && k + 1 < sq->count[i] && k + 1 < GMSQUAD_SIZE ?
                               (int8_t)sq->lineup[i][k + 1].ckind :
                               -1;
        }
    }
    return true;
}

static int by_newest(const void* a, const void* b) {
    const PcReplayInfo* x = a;
    const PcReplayInfo* y = b;
    return x->created < y->created ? 1 : x->created > y->created ? -1 : strcmp(y->file, x->file);
}

int pc_replay_list(PcReplayInfo* out, int max) {
    const char* dir = pc_replay_folder();
    if (dir[0] == '\0' || max <= 0) {
        return 0;
    }
    int count = 0;
    char** names = SDL_GlobDirectory(dir, "*.ssr", 0, &count);
    int n = 0;
    for (int i = 0; names != NULL && i < count && n < max; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (info_from(path, names[i], &out[n])) {
            n++;
        }
    }
    SDL_free(names);
    qsort(out, n, sizeof *out, by_newest);
    return n;
}
