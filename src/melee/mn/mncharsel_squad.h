/* Squad Strike on character select (PC only; included by mncharsel.c, whose
 * statics it reads). See src/melee/gm/gmsquad.h.
 *
 *   - Drop your coin on your first fighter as usual.
 *   - Point at another portrait and press Z to add it (up to three times).
 *     Carrying a coin, the portrait previews the fighter: pick its colour
 *     with X/Y first, and Z adds it in that colour (Z again on a member
 *     already in the squad gives it the previewed colour). With a free
 *     pointer, Z on a member already in your squad changes its colour.
 *   - X / Y while pointing at a squad member's portrait changes that
 *     member's colour (elsewhere they change your coin pick's, as usual).
 *   - R, or D-pad up (offline only: online the D-pad is quick chat),
 *     removes the last fighter you added. Holding L for 1.5 s clears the
 *     whole squad.
 *   - Z on the random area beside the portraits (where a dropped coin picks
 *     at random): still carrying your coin, it picks your first fighter at
 *     random; after that each press adds a random fighter not yet in the
 *     squad, so four presses make a fully random squad.
 *   - Holding a CPU's coin, Z builds that CPU's squad.
 *   - The error buzz: every other colour is taken, the squad is full, or
 *     there are more than two players (squads are for 1v1s).
 *
 * Everything here runs on the synced pads in netplay, so both peers build
 * the same squads. */

#include <stdlib.h>
#include <string.h>

#include <melee/gm/gm_1601.h>
#include <melee/gm/gmsquad.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/tobj.h>
#include "pc/net.h"

static const char* const squad_names[ChKind_Max] = {
    "FALCON", "DK",     "FOX",   "GNW",     "KIRBY", "BOWSER", "LINK",
    "LUIGI",  "MARIO",  "MARTH", "MEWTWO",  "NESS",  "PEACH",  "PIKACHU",
    "ICS",    "PUFF",   "SAMUS", "YOSHI",   "ZELDA", "SHEIK",  "FALCO",
    "YLINK",  "DOC",    "ROY",   "PICHU",   "GANON",
};

/* Colour words per character and costume, from the costume file names
 * (PlFxOr.dat = Fox, orange); "" for the default. Game & Watch's are tints. */
static const char* const squad_colors[26][6] = {
    { "", "GRY", "RED", "WHT", "GRN", "BLU" }, /* Falcon */
    { "", "BLK", "RED", "BLU", "GRN" },        /* DK */
    { "", "ORG", "PUR", "GRN" },               /* Fox */
    { "", "RED", "BLU", "GRN" },               /* Game & Watch */
    { "", "YEL", "BLU", "RED", "GRN", "WHT" }, /* Kirby */
    { "", "RED", "BLU", "BLK" },               /* Bowser */
    { "", "RED", "BLU", "BLK", "WHT" },        /* Link */
    { "", "WHT", "CYN", "PNK" },               /* Luigi */
    { "", "YEL", "BLK", "BLU", "GRN" },        /* Mario */
    { "", "RED", "GRN", "BLK", "WHT" },        /* Marth */
    { "", "RED", "BLU", "GRN" },               /* Mewtwo */
    { "", "YEL", "BLU", "GRN" },               /* Ness */
    { "", "YEL", "WHT", "BLU", "GRN" },        /* Peach */
    { "", "RED", "BLU", "GRN" },               /* Pikachu */
    { "", "GRN", "ORG", "RED" },               /* Ice Climbers */
    { "", "RED", "BLU", "GRN", "YEL" },        /* Jigglypuff */
    { "", "PNK", "BLK", "GRN", "PUR" },        /* Samus */
    { "", "RED", "BLU", "YEL", "PNK", "CYN" }, /* Yoshi */
    { "", "RED", "BLU", "GRN", "WHT" },        /* Zelda */
    { "", "RED", "BLU", "GRN", "WHT" },        /* Sheik */
    { "", "RED", "BLU", "GRN" },               /* Falco */
    { "", "RED", "BLU", "WHT", "BLK" },        /* Young Link */
    { "", "RED", "BLU", "GRN", "BLK" },        /* Dr. Mario */
    { "", "RED", "BLU", "GRN", "YEL" },        /* Roy */
    { "", "RED", "BLU", "GRN" },               /* Pichu */
    { "", "RED", "BLU", "GRN", "PUR" },        /* Ganondorf */
};

static int squad_num_colors(u8 ck)
{
    int n = 0;
    if (ck >= 26) {
        return 1;
    }
    while (n < 6 && squad_colors[ck][n] != NULL) {
        n++;
    }
    return n;
}

/* The canvas character select draws its text on (set in mnCharSel_802640A0)
 * and our lineup banner per door. Both die with the screen. */
static int squad_canvas = -1;
static HSD_Text* squad_texts[4];
static u32 squad_shown[4];

/* Stock icons for the lineup, from the in-match HUD file (IfAll.dat), one
 * row per door where the text banner would be. If the file can't be loaded
 * (or MELEE_SQUAD_ICONS=0), the text banner is shown instead. Layout knobs,
 * settable in melee-env.txt next to melee.exe while it's being tuned:
 *   MELEE_SQUAD_ICON_X, _Y, _Z   offset of the row from its default place
 *   MELEE_SQUAD_ICON_SCALE       icon size (1 = the HUD's size)
 *   MELEE_SQUAD_ICON_GAP         distance between icon centres */
#define SQUAD_ICONS_MAX GMSQUAD_SIZE
static HSD_Archive* squad_icon_archive;
static DynamicModelDesc* squad_icon_model;
static HSD_GObj* squad_icons[4][SQUAD_ICONS_MAX];
static u32 squad_icons_shown[4];
/* squad_icons_load has run on this screen with Squad Strike on. */
static bool squad_icons_tried;
/* Per door, the hover squad_preview_member last handled. */
static u8 squad_seen_icon[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static f32 squad_icon_dx, squad_icon_dy, squad_icon_dz;
static f32 squad_icon_scale = 0.9f;
static f32 squad_icon_gap = 2.3f;

static f32 squad_env(const char* name, f32 fallback)
{
    const char* v = getenv(name);
    if (v == NULL || v[0] == '\0') {
        return fallback;
    }
    return (f32) atof(v);
}

static void squad_icons_load(void)
{
    const char* off = getenv("MELEE_SQUAD_ICONS");
    DiscU32* scene_models = NULL;

    squad_icon_archive = NULL;
    squad_icon_model = NULL;
    if (mnCharSel_804D6CB0 == NULL ||
        mnCharSel_804D6CB0->match_type != VS_MELEE || !gmSquad_ModeAllowed())
    {
        return; /* tried again once Squad Strike is switched on */
    }
    squad_icons_tried = true;
    if (off != NULL && off[0] == '0') {
        return;
    }
    squad_icon_dx = squad_env("MELEE_SQUAD_ICON_X", 0.0f);
    squad_icon_dy = squad_env("MELEE_SQUAD_ICON_Y", 0.0f);
    squad_icon_dz = squad_env("MELEE_SQUAD_ICON_Z", 0.0f);
    squad_icon_scale = squad_env("MELEE_SQUAD_ICON_SCALE", 0.9f);
    squad_icon_gap = squad_env("MELEE_SQUAD_ICON_GAP", 2.3f);
    /* Returns whether it was preloaded, not whether it loaded. */
    lbArchive_80016F80(&squad_icon_archive, "IfAll");
    if (squad_icon_archive == NULL) {
        squad_icon_archive = NULL;
        OSReport("squad: IfAll not loaded, using the text banner\n");
        return;
    }
    lbArchive_LoadSections(squad_icon_archive, &scene_models, "Stc_scemdls",
                           0);
    if (scene_models != NULL) {
        squad_icon_model = DP(DynamicModelDesc, scene_models[0].v);
    }
    if (squad_icon_model == NULL) {
        OSReport("squad: no stock icon model, using the text banner\n");
    }
}

static void mnCharSel_SquadReset(int canvas)
{
    int i, j;
    squad_canvas = canvas;
    for (i = 0; i < 4; i++) {
        squad_texts[i] = NULL;
        squad_shown[i] = 0;
        squad_icons_shown[i] = 0;
        squad_seen_icon[i] = 0xFF;
        for (j = 0; j < SQUAD_ICONS_MAX; j++) {
            squad_icons[i][j] = NULL; /* freed with the last screen */
        }
    }
    squad_icons_tried = false;
    squad_icons_load();
}

/* The character and colour a door's portrait shows right now. While a coin
 * is carried, that is the portrait under it (X/Y recolour it there too), not
 * the coin pick. Returns false if the door shows no character. */
static bool squad_door_shows(int port, u8* ck, u8* color)
{
    struct CSSDoor* door = &mnCharSel_803F0DFC.doors[port];
    if (door->sel_icon >= 0x19) {
        return false;
    }
    *ck = icons[door->sel_icon].char_kind;
    *color = door->costume;
    return *ck < 26;
}

/* A coin pick's colour as character select has it right now: the door's
 * costume when the door shows the coin pick (X/Y change it at once;
 * players[].color only follows on leaving), else the last colour the game
 * recorded. Always a colour that character has, so its stock icon is its
 * own (an out-of-range colour picked another character's icon frame). */
static u8 squad_coin_color(int port)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[port];
    u8 ck = (u8) pl->ckind;
    u8 shown_ck, shown_color;
    u8 color = pl->color;
    if (squad_door_shows(port, &shown_ck, &shown_color) && shown_ck == ck) {
        color = shown_color;
    }
    if (ck < 26 && color >= squad_num_colors(ck)) {
        color = 0;
    }
    return color;
}

static bool squad_door_open(int port)
{
    u8 type = mnCharSel_804D6CB0->vs.start.players[port].slot_type;
    return type == Gm_PKind_Human || type == Gm_PKind_Cpu;
}

static int squad_players(void)
{
    int n = 0;
    int p;
    for (p = 0; p < 4; p++) {
        if (squad_door_open(p)) {
            n++;
        }
    }
    return n;
}

/* Another port has this character in this colour (coin pick or member). */
static bool squad_clash(int port, u8 ck, u8 color)
{
    int p, i;
    for (p = 0; p < 4; p++) {
        PlayerInitData* pl;
        if (p == port || !squad_door_open(p)) {
            continue;
        }
        pl = &mnCharSel_804D6CB0->vs.start.players[p];
        if ((u8) pl->ckind == ck && squad_coin_color(p) == color) {
            return true;
        }
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            if (gmSquad_rows[p][i].ckind == ck &&
                gmSquad_rows[p][i].color == color)
            {
                return true;
            }
        }
    }
    return false;
}

/* Give a member the first colour from its own (+step) on that nobody else
 * has. Returns true if its colour changed; false if none is free. */
static bool squad_fix_color(int port, int index, int step)
{
    GmSquadMember* m = &gmSquad_rows[port][index];
    int n = squad_num_colors(m->ckind);
    int tries;
    if (m->ckind >= ChKind_Max) {
        return false;
    }
    for (tries = 0; tries < n; tries++) {
        u8 c = (u8) ((m->color + step + tries) % n);
        if (!squad_clash(port, m->ckind, c)) {
            if (c == m->color) {
                return false;
            }
            m->color = c;
            return true;
        }
    }
    return false;
}

/* Move a member to the next (dir 1) or previous (dir -1) colour nobody else
 * has. Returns false if no other colour is free. */
static bool squad_cycle_color(int port, int index, int dir)
{
    GmSquadMember* m = &gmSquad_rows[port][index];
    int n = squad_num_colors(m->ckind);
    int t;
    if (m->ckind >= ChKind_Max) {
        return false;
    }
    for (t = 1; t < n; t++) {
        u8 c = (u8) (((int) m->color + dir * t + n * t) % n);
        if (!squad_clash(port, m->ckind, c)) {
            m->color = c;
            return true;
        }
    }
    return false;
}

static bool squad_remove_last(int port)
{
    int i;
    for (i = GMSQUAD_EXTRA - 1; i >= 0; i--) {
        if (gmSquad_rows[port][i].ckind != GMSQUAD_NONE) {
            gmSquad_rows[port][i].ckind = GMSQUAD_NONE;
            gmSquad_rows[port][i].color = 0;
            return true;
        }
    }
    return false;
}

/* The portrait under the cursor's coin position, or -1. */
static int squad_hovered(struct CSSCursorData* cursor)
{
    /* Where a coin dropped from the pointer would land. */
    f32 x = cursor->xC + 2.7f;
    f32 y = cursor->x10 - 2.0f;
    int i;
    for (i = 0; i < 25; i++) {
        CSSIcon* icon = &icons[i];
        if (icon->state < 1) {
            continue;
        }
        if (x > icon->bound_l && x < icon->bound_r && y < icon->bound_u &&
            y > icon->bound_d)
        {
            return icon->char_kind;
        }
    }
    return -1;
}

/* Which squad member this port's free pointer is on: its own first, else a
 * CPU's (whose squad this player builds by holding the CPU's coin). Returns
 * the member's index and sets *owner to its port, or returns -1. */
static int squad_member_under_cursor(int port, int* owner)
{
    struct CSSCursorData* cursor;
    int ck, i, q;
    if (port < 0 || port >= 4 || mnCharSel_804D6CB0 == NULL ||
        mnCharSel_804D6CB0->match_type != VS_MELEE || mnCharSel_804D6CF5 == 1 ||
        !gmSquad_ModeAllowed())
    {
        return -1;
    }
    cursor = mnCharSel_804A0BC0[port];
    if (cursor == NULL || cursor->x5 != 2) {
        return -1;
    }
    ck = squad_hovered(cursor);
    if (ck < 0) {
        return -1;
    }
    for (q = 0; q < 4; q++) {
        int who = (port + q) % 4; /* own squad first */
        if (who != port && mnCharSel_803F0DFC.doors[who].p_kind != 1) {
            continue; /* other humans recolour their own members */
        }
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            if (gmSquad_rows[who][i].ckind == (u8) ck) {
                *owner = who;
                return i;
            }
        }
    }
    return -1;
}

/* mnCharSel_CostumeChange asks: is this port's X/Y ours this frame? */
static bool mnCharSel_SquadTakesXY(int port)
{
    int owner;
    return squad_member_under_cursor(port, &owner) >= 0;
}

static void squad_add(int target, u8 ck)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[target];
    int i;
    u8 shown_ck, shown_color;
    /* Carrying a coin over the portrait, the door previews this fighter in
     * the colour X/Y chose: that is the colour Z adds it in. */
    bool preview = squad_door_shows(target, &shown_ck, &shown_color) &&
                   shown_ck == ck && shown_color < squad_num_colors(ck);

    if ((u8) pl->ckind == ck) {
        lbAudioAx_80024030(3); /* already your first fighter (R removes) */
        return;
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        if (gmSquad_rows[target][i].ckind == ck) {
            if (preview && gmSquad_rows[target][i].color != shown_color &&
                !squad_clash(target, ck, shown_color))
            {
                /* Already in the squad: Z takes the previewed colour. */
                gmSquad_rows[target][i].color = shown_color;
                sfxMove();
            } else if (squad_fix_color(target, i, 1)) {
                sfxMove();
            } else {
                lbAudioAx_80024030(3); /* no other colour free */
            }
            return;
        }
    }
    if (squad_players() > 2) {
        lbAudioAx_80024030(3); /* squads are for 1v1s */
        return;
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        if (gmSquad_rows[target][i].ckind == GMSQUAD_NONE) {
            gmSquad_rows[target][i].ckind = ck;
            gmSquad_rows[target][i].color = preview ? shown_color : 0;
            squad_fix_color(target, i, 0);
            sfxForward();
            return;
        }
    }
    lbAudioAx_80024030(3); /* squad full */
}

/* Frames L is held to clear a squad: 1.5 s. */
#define SQUAD_CLEAR_HOLD 90
static u8 squad_l_held[4];

/* The random area either side of the portraits' bottom row: a coin dropped
 * there picks at random (mncharsel.c, the A press). Same coordinates as
 * squad_hovered: where the coin would land. */
static bool squad_on_random(struct CSSCursorData* cursor)
{
    f32 x = cursor->xC + 2.7f;
    f32 y = cursor->x10 - 2.0f;
    return y < 6.0f && y > -1.0f &&
           ((x > -30.0f && x < -24.4f) || (x > 24.4f && x < 30.2f));
}

/* A random number both netplay peers agree on: the session seed, the synced
 * frame and who asked. Offline, the game's own generator. */
static u32 squad_random(int port, int salt)
{
    if (pc_net_active()) {
        u32 h = pc_net_seed() ^ ((u32) pc_net_frame() * 2654435761u) ^
                ((u32) (port * 97 + salt) * 40503u);
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }
    return (u32) HSD_Randi(0x7FFFFFFF);
}

/* Z on the random area. Carrying the door's own coin with nothing picked:
 * the game's random coin pick. Otherwise a random fighter the squad does not
 * have yet joins it. */
static void squad_random_pick(int target, struct CSSCursorData* cursor)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[target];
    u8 pool[25];
    int n = 0, i, k;

    if (cursor->x5 == 1 && cursor->x6 == target && (u8) pl->ckind >= 26) {
        /* As dropping the coin on the random area with A does. */
        mnCharSel_8025FB50((u8) target, 0);
        do {
            mnCharSel_803F0DFC.doors[target].costume = HSD_Randi((s32) gm_GetNumCostumesForCKind(
                icons[mnCharSel_803F0DFC.doors[target].sel_icon].char_kind));
        } while (isDuplicateCostume(target));
        mnCharSel_8025DB34((u8) target);
        cursor->x5 = 2;
        lbAudioAx_800237A8(0xB8, 0x7F, 0x40);
        return;
    }
    if ((u8) pl->ckind >= 26) {
        lbAudioAx_80024030(3); /* pick a first fighter first */
        return;
    }
    for (i = 0; i < 25; i++) {
        u8 ck = icons[i].char_kind;
        bool have = icons[i].state < 1 || ck >= 26 || ck == (u8) pl->ckind;
        for (k = 0; k < GMSQUAD_EXTRA && !have; k++) {
            have = gmSquad_rows[target][k].ckind == ck;
        }
        if (!have) {
            pool[n++] = ck;
        }
    }
    if (n == 0 || gmSquad_Count(target) >= GMSQUAD_EXTRA) {
        lbAudioAx_80024030(3); /* squad full */
        return;
    }
    squad_add(target, pool[squad_random(target, gmSquad_Count(target)) % (u32) n]);
}

static void squad_buttons(int port)
{
    struct CSSCursorData* cursor = mnCharSel_804A0BC0[port];
    u32 pressed;
    int target;

    if (cursor == NULL) {
        return;
    }
    pressed = HSD_PadCopyStatus[port].trigger;
    if (!(HSD_PadCopyStatus[port].button & HSD_PAD_L)) {
        squad_l_held[port] = 0;
    } else if (squad_l_held[port] < 0xFF) {
        squad_l_held[port]++;
    }
    if (pressed & (HSD_PAD_X | HSD_PAD_Y)) {
        /* Pointing at one of your members: X/Y recolour it, like the coin
         * pick (mnCharSel_CostumeChange leaves these presses to us). */
        int owner = port;
        int index = squad_member_under_cursor(port, &owner);
        if (index >= 0) {
            if (squad_cycle_color(owner, index,
                                  (pressed & HSD_PAD_X) ? 1 : -1))
            {
                sfxMove();
            } else {
                lbAudioAx_80024030(3);
            }
            return;
        }
    }
    if (!(pressed & (HSD_PAD_Z | HSD_PAD_R | HSD_PAD_DPADUP)) &&
        squad_l_held[port] != SQUAD_CLEAR_HOLD)
    {
        return;
    }
    if (cursor->x5 == 2) { /* free pointer: your own squad */
        target = port;
    } else if (cursor->x5 == 1 && cursor->x6 <= 3) {
        target = cursor->x6; /* holding a coin: that coin's squad */
    } else {
        return;
    }
    if (!squad_door_open(target)) {
        return;
    }
    if (squad_l_held[port] == SQUAD_CLEAR_HOLD) {
        /* Held for 1.5 s: the whole squad goes, once per hold. */
        if (gmSquad_Count(target) != 0) {
            gmSquad_Clear(target);
            sfxBack();
        }
        return;
    }
    /* Online the D-pad is quick chat (src/pc/net_chat.c). */
    if ((pressed & HSD_PAD_R) || ((pressed & HSD_PAD_DPADUP) && !pc_net_active())) {
        if (squad_remove_last(target)) {
            sfxBack();
        } else {
            lbAudioAx_80024030(3);
        }
        return;
    }
    if (pressed & HSD_PAD_DPADUP) {
        return;
    }
    if (squad_on_random(cursor)) {
        squad_random_pick(target, cursor);
        return;
    }
    {
        int ck = squad_hovered(cursor);
        if (ck >= 0 && ck < 26) {
            squad_add(target, (u8) ck);
        }
    }
}

static void squad_line(int port, char* out, size_t size)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[port];
    int i;
    out[0] = '\0';
    if (gmSquad_Count(port) == 0) {
        return;
    }
    if ((u8) pl->ckind < 26) {
        strncat(out, squad_names[(u8) pl->ckind], size - strlen(out) - 1);
    } else {
        strncat(out, "?", size - strlen(out) - 1);
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        GmSquadMember* m = &gmSquad_rows[port][i];
        const char* word;
        if (m->ckind >= 26) {
            continue;
        }
        strncat(out, " ", size - strlen(out) - 1);
        word = m->color < squad_num_colors(m->ckind)
                   ? squad_colors[m->ckind][m->color]
                   : "";
        if (word[0] != '\0') {
            strncat(out, word, size - strlen(out) - 1);
            strncat(out, " ", size - strlen(out) - 1);
        }
        strncat(out, squad_names[m->ckind], size - strlen(out) - 1);
    }
}

/* One stock icon, as the HUD makes them (ifStock_802F96D0), drawn by the
 * character select camera with the character models. */
/* y is a reference height (where the first version put the model, at
 * nametag + 2.2); rise is how far the icon's centre goes above the nametag's
 * centre. The first version's icons (scale 0.8) measured 10.1 units above
 * it, which pins the nametag's centre relative to the model whatever the
 * icon's offset inside the model is. */
static HSD_GObj* squad_icon_create(u8 ck, u8 color, f32 x, f32 y, f32 z,
                                   f32 rise)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    HSD_JObj* icon = NULL;
    HSD_TObj* tobj;

    gobj = GObj_Create(4, 5, 0x80);
    if (gobj == NULL) {
        return NULL;
    }
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, squad_icon_model->joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 2, 0x80);
    gm_8016895C(jobj, squad_icon_model, 0);
    lb_80011E24(jobj, &icon, 1, -1);
    HSD_JObjSetTranslateX(jobj, x);
    HSD_JObjSetTranslateY(jobj, y);
    HSD_JObjSetTranslateZ(jobj, z);
    HSD_JObjSetScaleX(jobj, squad_icon_scale);
    HSD_JObjSetScaleY(jobj, squad_icon_scale);
    HSD_JObjSetScaleZ(jobj, squad_icon_scale);
    HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
    HSD_JObjReqAnimAll(jobj, 0.0f);
    if (icon != NULL) {
        HSD_JObjClearFlags(icon, JOBJ_HIDDEN);
        if (icon->u.dobj != NULL && icon->u.dobj->mobj != NULL &&
            (tobj = icon->u.dobj->mobj->tobj) != NULL)
        {
            HSD_TObjReqAnimAll(tobj, gm_80168B34(ck, 0, color));
            if (tobj->aobj != NULL) {
                HSD_AObjSetRate(tobj->aobj, 0.0f);
            }
        }
    }
    HSD_JObjAnimAll(jobj);
    /* The icon sits off its model's origin (the HUD lays several out in a
     * row from one origin): move the model so the icon itself is centred on
     * (x, y). */
    if (icon != NULL) {
        Vec3 at;
        lb_8000B1CC(icon, NULL, &at);
        HSD_JObjSetTranslateX(jobj, x + (x - at.x));
        {
            f32 off = (at.y - y) / squad_icon_scale; /* icon within model */
            f32 tag_centre = y + off * 0.8f - 10.1f;
            HSD_JObjSetTranslateY(jobj, y + (tag_centre + rise - at.y));
        }
        HSD_JObjAnimAll(jobj);
    }
    return gobj;
}

/* A row of stock icons above door p's nametag: coin pick, then members. */
static bool squad_icon_row(int p)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
    u8 cks[SQUAD_ICONS_MAX];
    u8 colors[SQUAD_ICONS_MAX];
    int n = 0;
    int i;
    u32 key = 2166136261u;
    HSD_JObj* name_jobj = NULL;
    Vec3 pos;
    f32 x0;

    if (squad_icon_model == NULL && !squad_icons_tried) {
        /* The screen opened with Squad Strike off (switched on since, from
         * the in-game menu): load the icons now. */
        squad_icons_load();
    }
    if (squad_icon_model == NULL || mnCharSel_804D6CC0 == NULL) {
        return false;
    }
    if (gmSquad_Count(p) != 0) {
        cks[n] = (u8) pl->ckind < 26 ? (u8) pl->ckind : GMSQUAD_NONE;
        colors[n] = squad_coin_color(p);
        n++;
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            if (gmSquad_rows[p][i].ckind < 26) {
                cks[n] = gmSquad_rows[p][i].ckind;
                colors[n] = gmSquad_rows[p][i].color;
                n++;
            }
        }
    }
    for (i = 0; i < n; i++) {
        key = (key ^ cks[i]) * 16777619u;
        key = (key ^ colors[i]) * 16777619u;
    }
    key = (key ^ (u32) n) * 16777619u;
    key = (key ^ mnCharSel_803F0DFC.doors[p].p_kind) * 16777619u;
    key = (key ^ gmMainLib_GetGameRules()->handicap) * 16777619u;
    key |= 1;
    if (key == squad_icons_shown[p]) {
        return true;
    }
    squad_icons_shown[p] = key;

    for (i = 0; i < SQUAD_ICONS_MAX; i++) {
        if (squad_icons[p][i] != NULL) {
            HSD_GObjFree(squad_icons[p][i]);
            squad_icons[p][i] = NULL;
        }
    }
    if (n == 0) {
        return true;
    }

    /* The row sits just above the nametag (whose joint is at the tag's
     * left end, vertically centred), or above the CPU level / handicap
     * slider when the door shows one, so it covers as little of the
     * character art as it can. Measured on a 2560x1440 screen: 1 unit is
     * about 14 px of a 1456-wide frame. */
    lb_80011E24(mnCharSel_804D6CC0, &name_jobj,
                mnCharSel_803F0E8C[p].name_jointl, -1);
    if (name_jobj == NULL) {
        return false;
    }
    lb_8000B1CC(name_jobj, NULL, &pos);
    {
        f32 above = 2.4f; /* tag half-height + icon half-height + a gap */
        bool cpu = mnCharSel_803F0DFC.doors[p].p_kind == 1;
        bool handicap = gmMainLib_GetGameRules()->handicap != 0;
        if (cpu || handicap) {
            above += 4.2f; /* "CPU Level" / handicap slider */
        }
        if (cpu && handicap) {
            above += 2.6f; /* both sliders */
        }
        x0 = pos.x + 4.4f + squad_icon_dx - squad_icon_gap * (n - 1) / 2.0f;
        for (i = 0; i < n; i++) {
            if (cks[i] == GMSQUAD_NONE) {
                continue; /* no coin pick yet: leave its place empty */
            }
            squad_icons[p][i] = squad_icon_create(
                cks[i], colors[i], x0 + squad_icon_gap * i,
                pos.y + 2.2f, pos.z + 0.5f + squad_icon_dz,
                above + squad_icon_dy);
        }
    }
    {
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("squad: icons for door %d at nametag (%.2f, %.2f, %.2f)"
                     "\n",
                     p, pos.x, pos.y, pos.z);
        }
    }
    return true;
}

/* One banner per door, just under its nametag: "MARTH ORG FOX RED FALCO".
 * With stock icons available, the icons replace it. */
static void squad_banners(void)
{
    int p;

    if (squad_canvas < 0 || mnCharSel_804D6CF5 == 1) {
        return;
    }
    for (p = 0; p < 4; p++) {
        char line[96];
        u32 key;
        size_t k;
        HSD_Text* text = squad_texts[p];
        if (squad_icon_row(p)) {
            continue; /* icons drawn: no text banner */
        }
        if (text == NULL) {
            CSSTagData* tag = mnCharSel_803F0E8C[p].data;
            if (tag == NULL || tag->text == NULL) {
                continue;
            }
            text = squad_texts[p] = HSD_SisLib_803A6754(0, squad_canvas);
            if (text == NULL) {
                continue;
            }
            text->pos_x = tag->text->pos_x - 1.86f;
            text->pos_y = tag->text->pos_y - 2.6f;
            text->pos_z = tag->text->pos_z;
            text->box_size_x = 288.89f;
            text->box_size_y = 36.0f;
            text->font_size.x = 0.045f;
            text->font_size.y = 0.045f;
            text->default_fitting = 1;
            text->default_alignment = 1;
            text->x4C = 1;
            text->bg_color.r = text->bg_color.g = text->bg_color.b = 0;
            text->bg_color.a = 0;
            HSD_SisLib_803A6B98(text, 144.44f, 0.0f, "");
            squad_shown[p] = 0;
        }
        squad_line(p, line, sizeof(line));
        key = 2166136261u;
        for (k = 0; line[k] != '\0'; k++) {
            key = (key ^ (u8) line[k]) * 16777619u;
        }
        key |= 1;
        if (key == squad_shown[p]) {
            continue;
        }
        squad_shown[p] = key;
        if (line[0] != '\0') {
            text->bg_color.r = text->bg_color.g = text->bg_color.b = 0x10;
            text->bg_color.a = 0xB8;
        } else {
            text->bg_color.a = 0;
        }
        HSD_SisLib_803A70A0(text, 0, "%s", line);
    }
}

/* Carrying a coin onto a fighter already in that squad: the portrait
 * previews the member in its own colour (the game starts every new hover on
 * the first free colour), so X/Y and Z carry on from it. Runs once per new
 * hover, the frame after the game takes it, so X/Y still work after. */
static void squad_preview_member(int p)
{
    struct CSSDoor* door = &mnCharSel_803F0DFC.doors[p];
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[p];
    u8 ck, color;
    int i;

    if (door->sel_icon == squad_seen_icon[p] ||
        door->sel_icon != door->sel_icon_prev)
    {
        return; /* no new hover, or the game hasn't taken it yet */
    }
    squad_seen_icon[p] = door->sel_icon;
    if (!squad_door_shows(p, &ck, &color) || ck == (u8) pl->ckind) {
        return; /* nothing shown, or it's the coin pick */
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        GmSquadMember* m = &gmSquad_rows[p][i];
        if (m->ckind == ck && m->color != color &&
            m->color < squad_num_colors(ck))
        {
            door->costume = m->color;
            mnCharSel_8025DB34((u8) p);
            return;
        }
    }
}

/* One stock per fighter of the biggest squad, as the match will give
 * everyone; the player's own stock count comes back when the last squad is
 * gone. */
static void squad_sync_stocks(void)
{
    static int saved_stocks = -1;
    GameRules* rules = gmMainLib_GetGameRules();
    if (gmSquad_Any() && rules->mode == Mode_Stock) {
        if (saved_stocks < 0) {
            saved_stocks = rules->stock_count;
        }
        rules->stock_count = (u8) gmSquad_StocksFor();
    } else if (saved_stocks >= 0) {
        if (rules->mode == Mode_Stock) {
            rules->stock_count = (u8) saved_stocks;
        }
        saved_stocks = -1;
    }
}

/* Runs at the top of mnCharSel_Scene_OnFrame, before the game hands this
 * frame's coin picks to the preloader. */
static void mnCharSel_SquadFrame(void)
{
    int p, i;

    if (mnCharSel_804D6CB0 == NULL) {
        return;
    }
    gmSquad_CheckSession();
    if (mnCharSel_804D6CB0->match_type != VS_MELEE || mnCharSel_804D6CF5 == 1) {
        return;
    }
    if (!gmSquad_ModeAllowed()) {
        /* Squad Strike switched off (launcher or in-game menu): drop any
         * squads, their icons, and the stock count they set. */
        gmSquad_ClearAll();
        lbDvd_GetPreloadCacheScene()->game_cache.pc_squad_key = gmSquad_Key();
        squad_sync_stocks();
        squad_banners();
        return;
    }

    for (p = 0; p < 4; p++) {
        if (!squad_door_open(p)) {
            gmSquad_Clear(p); /* a closed door has no squad */
            continue;
        }
        squad_preview_member(p);
        if (mnCharSel_804D6CF6 == 0) {
            squad_buttons(p);
        }
    }

    if (squad_players() > 2 && gmSquad_Any()) {
        gmSquad_ClearAll(); /* a third player joined: squads are for 1v1s */
        lbAudioAx_80024030(3);
    }

    /* No two fighters may share a character and colour: when a coin pick or
     * another member moves onto a member's, that member's colour moves on. */
    for (p = 0; p < 4; p++) {
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            squad_fix_color(p, i, 0);
        }
    }

    lbDvd_GetPreloadCacheScene()->game_cache.pc_squad_key = gmSquad_Key();

    squad_sync_stocks();
    squad_banners();
}
