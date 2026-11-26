/* The 20XX HackPack's SD Remix Captain Falcon on character select (PC only;
 * included by mncharsel.c after mncharsel_squad.h, whose canvas and helpers
 * it uses). See src/melee/ft/ftvariant.h.
 *
 *   - D-pad left, with the door showing Captain Falcon (coin on him, or
 *     pointing at him with your coin in hand), switches that door between
 *     the usual Falcon and the 20XX one. Holding a CPU's coin, it switches
 *     the CPU's. A "20XX FALCON" tag under the door's name says which is picked.
 *   - The pick stays with the port for any Falcon it plays, squad members
 *     included, until switched back.
 *   - Online the D-pad is quick chat: while your door shows Falcon, D-pad
 *     left is his switch, and chat's LEFT choices wait until it doesn't
 *     (pc_chat_left_reserved_frame).
 *
 * It runs on the synced pads in netplay, so both peers pick the same. */

#include <melee/ft/ftvariant.h>

#include "pc/pc.h"

static HSD_Text* variant_texts[4];
static u8 variant_shown[4];

static void mnCharSel_VariantReset(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        variant_texts[i] = NULL; /* freed with the last screen */
        variant_shown[i] = 0xFF;
    }
}

/* Whether a port plays Captain Falcon: its coin pick, the portrait its
 * door shows, or a squad member. */
static bool variant_has_falcon(int port, bool shown_only)
{
    u8 ck, color;
    int i;
    if (squad_door_shows(port, &ck, &color) && ck == CKind_Captain) {
        return true;
    }
    if (shown_only) {
        return false;
    }
    if (squad_coin_pick(port) == CKind_Captain) {
        return true;
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        if (gmSquad_rows[port][i].ckind == CKind_Captain) {
            return true;
        }
    }
    return false;
}

static void variant_buttons(int port)
{
    struct CSSCursorData* cursor = mnCharSel_804A0BC0[port];
    int target;
    if (cursor == NULL ||
        !(HSD_PadCopyStatus[port].trigger & HSD_PAD_DPADLEFT))
    {
        return;
    }
    if (cursor->x5 == 1 && cursor->x6 <= 3) {
        target = cursor->x6; /* holding a coin: that door's */
    } else {
        target = port;
    }
    if (!squad_door_open(target) || !variant_has_falcon(target, true)) {
        return;
    }
    ftVariant_ports[target] ^= FTVARIANT_FALCON_20XX;
    sfxMove();
}

static void variant_tags(void)
{
    int p;
    if (squad_canvas < 0 || mnCharSel_804D6CF5 == 1) {
        return;
    }
    for (p = 0; p < 4; p++) {
        HSD_Text* text = variant_texts[p];
        u8 show = squad_door_open(p) && variant_has_falcon(p, false) &&
                  (ftVariant_ports[p] & FTVARIANT_FALCON_20XX);
        if (text == NULL) {
            CSSTagData* tag = mnCharSel_803F0E8C[p].data;
            if (tag == NULL || tag->text == NULL) {
                continue;
            }
            text = variant_texts[p] = HSD_SisLib_803A6754(0, squad_canvas);
            if (text == NULL) {
                continue;
            }
            text->pos_x = tag->text->pos_x - 1.86f;
            text->pos_y = tag->text->pos_y + 3.1f;
            text->pos_z = tag->text->pos_z;
            text->box_size_x = 288.89f;
            text->box_size_y = 36.0f;
            text->font_size.x = 0.042f;
            text->font_size.y = 0.042f;
            text->default_fitting = 1;
            text->default_alignment = 1;
            text->x4C = 1;
            text->bg_color.r = text->bg_color.g = text->bg_color.b = 0;
            text->bg_color.a = 0;
            HSD_SisLib_803A6B98(text, 144.44f, 0.0f, "");
            variant_shown[p] = 0xFF;
        }
        if (show == variant_shown[p]) {
            continue;
        }
        variant_shown[p] = show;
        if (show) {
            text->bg_color.r = 0x60;
            text->bg_color.g = 0x10;
            text->bg_color.b = 0x08;
            text->bg_color.a = 0xD0;
            HSD_SisLib_803A70A0(text, 0, "%s", "20XX FALCON");
        } else {
            text->bg_color.a = 0;
            HSD_SisLib_803A70A0(text, 0, "%s", "");
        }
    }
}

/* Runs at the top of mnCharSel_Scene_OnFrame, after the squad's frame. */
static void mnCharSel_VariantFrame(void)
{
    static u32 session;
    u32 now_session;
    int p;

    if (mnCharSel_804D6CB0 == NULL ||
        mnCharSel_804D6CB0->match_type != VS_MELEE || mnCharSel_804D6CF5 == 1)
    {
        return;
    }
    /* Both peers start a session with the disc's fighters. */
    now_session = pc_net_active() ? (pc_net_seed() | 1u) : 0;
    if (now_session != session) {
        ftVariant_Reset();
        session = now_session;
    }
    if (mnCharSel_804D6CF6 == 0) {
        for (p = 0; p < 4; p++) {
            variant_buttons(p);
        }
    }
    if (pc_net_active()) {
        int me = pc_net_local_player();
        if (me >= 0 && me < 4 && variant_has_falcon(me, true)) {
            pc_chat_left_reserved_frame = pc_frame_count();
        }
    }
    variant_tags();
}
