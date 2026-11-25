/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Match replays (src/pc/replay.c): every VS match, offline or online, is
 * saved as a small .ssr file -- the match setup plus each tick's controller
 * inputs and RNG seed -- and played back by re-simulating it.
 *
 * Recording is on unless the launcher's "Save replays" is off
 * (MELEE_REPLAYS=0 overrides). Files go to <pref path>/replays.
 * MELEE_REPLAY=<file> boots straight into that replay instead of the game. */
#ifndef PC_REPLAY_H
#define PC_REPLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct StartMeleeData;
struct PADStatus;

/* Startup, before the game's files load: reads MELEE_REPLAY. */
void pc_replay_init(void);

/* This run is a replay playback (the whole process). */
bool pc_replay_playing(void);
/* A replay is recording or playing the match that is running. Game code
 * uses it to keep the simulation independent of the render, as netplay
 * does (pc_sim_render_decoupled). */
bool pc_replay_match_active(void);

/* gmvs.c fn_8016E730 / gm_Scene_Vs_OnExit. */
void pc_replay_match_start(const struct StartMeleeData* data);
void pc_replay_match_end(void);

/* HSD_PadRenewMasterStatus, interrupts off. `q` is the queue entry (four
 * PADStatus) the tick is about to consume, or NULL when the queue is empty.
 * Recording: notes it. Playback: returns the recorded four pads to consume
 * instead (or NULL with *skip set when the recording consumed nothing). */
const struct PADStatus* pc_replay_renew(const struct PADStatus* q, bool* skip);

/* onEnterDebugVs: fill the match setup from the replay. */
void pc_replay_setup_match(struct StartMeleeData* start);

/* While a replay plays: the game mode the match was recorded in (the
 * playback itself runs as GM_DEBUG_VS), else -1. */
int pc_replay_mode_override(void);

/* Settings the recording carries, while one plays. Each returns false when
 * no replay is playing, leaving the caller to its own value. */
bool pc_replay_rule_flags(unsigned* flags); /* PC_RULE_* */
bool pc_replay_ucf(bool* on);
bool pc_replay_free_camera(bool* on);
bool pc_replay_online_rules(bool* on); /* Slippi's online-only fixes */
bool pc_replay_sdremix(bool* on);
bool pc_replay_unlock_all(bool* on);

/* Render-coupled retail behaviours give way to reproducible ones: netplay,
 * replay recording or playback, sync tests. */
bool pc_sim_render_decoupled(void);
/* Slippi's online ruleset (wobble limit, freeze glitch fix): online play
 * and the playback of a match that had it. */
bool pc_online_ruleset(void);

/* For the in-game display: the playback has diverged from the recording. */
bool pc_replay_diverged(void);

/* ---- the launcher's replay list ---- */
typedef struct PcReplayPlayer {
    int8_t ckind; /* ::CharacterKind; -1 for an empty slot */
    uint8_t color;
    uint8_t cpu; /* a CPU player */
    int8_t end_stocks;
    uint8_t team;
    int8_t squad[3]; /* the squad's other members' ::CharacterKind, -1 if none */
} PcReplayPlayer;

typedef struct PcReplayInfo {
    char path[1024];
    char file[128];  /* the file name alone */
    int64_t created; /* unix seconds, UTC */
    uint32_t frames; /* 60 a second */
    uint32_t bytes;
    uint8_t mode, online, sdremix, playable;
    uint16_t stkind;
    uint8_t ace_pick; /* GrAce_* when the stage was an ACE stage */
    uint8_t is_teams, stock_match;
    char name[32];     /* the recording machine's player name */
    char version[32];  /* the build that recorded it */
    int8_t local_port; /* the recording machine's player's port, -1 unknown */
    char opponent[16]; /* online: the opponent's connect code, or "" */
    PcReplayPlayer players[4];
} PcReplayInfo;

/* Every replay in the replay folder, newest first, up to `max`. */
int pc_replay_list(PcReplayInfo* out, int max);
/* The replay folder, created if missing. */
const char* pc_replay_folder(void);
/* Play this file when the game starts (the launcher's Watch), or with
 * `export_to` set, turn it into that video (Export Video). */
void pc_replay_request(const char* path, const char* export_to);
/* What the last export said, for the launcher after the relaunch. */
const char* pc_replay_status(void);
/* A replay is being turned into a video (MELEE_REPLAY_EXPORT). */
bool pc_replay_exporting(void);
/* While a replay plays: how many ticks to run this frame of the game's
 * own `count` (pause, slow motion, fast forward), from the controllers. */
int pc_replay_tick_budget(int count);
/* While a replay plays: its status line for the overlay; false when
 * nothing is to be shown. */
bool pc_replay_hud(char* buf, int len);

#ifdef __cplusplus
}
#endif

#endif
