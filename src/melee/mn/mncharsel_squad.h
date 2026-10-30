/* Squad Strike on character select (PC only; included by mncharsel.c, whose
 * statics it reads). See src/melee/gm/gmsquad.h.
 *
 *   - Drop your coin on your first fighter as usual.
 *   - Point at another portrait and press Z to add it (up to three times).
 *     Z on a fighter already in your squad changes its colour.
 *   - Z on your own coin pick's portrait, or D-pad up (offline only: online
 *     the D-pad is quick chat), removes the last fighter you added.
 *   - Holding a CPU's coin, Z builds that CPU's squad.
 *   - The error buzz: every other colour is taken, the squad is full, or
 *     there are more than two players (squads are for 1v1s).
 *
 * Everything here runs on the synced pads in netplay, so both peers build
 * the same squads. */

#include <string.h>

#include <melee/gm/gmsquad.h>
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

static void mnCharSel_SquadReset(int canvas)
{
    int i;
    squad_canvas = canvas;
    for (i = 0; i < 4; i++) {
        squad_texts[i] = NULL;
        squad_shown[i] = 0;
    }
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
        if ((u8) pl->ckind == ck && pl->color == color) {
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

static void squad_add(int target, u8 ck)
{
    PlayerInitData* pl = &mnCharSel_804D6CB0->vs.start.players[target];
    int i;

    if ((u8) pl->ckind == ck) {
        /* Your coin pick's portrait: take the last member back. */
        if (squad_remove_last(target)) {
            sfxBack();
        } else {
            lbAudioAx_80024030(3);
        }
        return;
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        if (gmSquad_rows[target][i].ckind == ck) {
            if (squad_fix_color(target, i, 1)) {
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
            gmSquad_rows[target][i].color = 0;
            squad_fix_color(target, i, 0);
            sfxForward();
            return;
        }
    }
    lbAudioAx_80024030(3); /* squad full */
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
    if (!(pressed & (HSD_PAD_Z | HSD_PAD_DPADUP))) {
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
    if (pressed & HSD_PAD_DPADUP) {
        /* Online the D-pad is quick chat (src/pc/net_chat.c). */
        if (!pc_net_active()) {
            if (squad_remove_last(target)) {
                sfxBack();
            } else {
                lbAudioAx_80024030(3);
            }
        }
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

/* One banner per door, just under its nametag: "MARTH ORG FOX RED FALCO". */
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

/* Runs at the top of mnCharSel_Scene_OnFrame, before the game hands this
 * frame's coin picks to the preloader. */
static void mnCharSel_SquadFrame(void)
{
    int p, i;

    if (mnCharSel_804D6CB0 == NULL) {
        return;
    }
    gmSquad_CheckSession();
    if (mnCharSel_804D6CB0->match_type != VS_MELEE || mnCharSel_804D6CF5 == 1 ||
        !gmSquad_ModeAllowed())
    {
        return;
    }

    for (p = 0; p < 4; p++) {
        if (!squad_door_open(p)) {
            gmSquad_Clear(p); /* a closed door has no squad */
            continue;
        }
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

    if (gmSquad_Any() && gmMainLib_GetGameRules()->mode == Mode_Stock) {
        /* One stock per fighter, as the match will give everyone. */
        gmMainLib_GetGameRules()->stock_count = GMSQUAD_STOCKS;
    }

    squad_banners();
}
