#include "mnstagesel.h"

#include <placeholder.h>

#include "forward.h"
#include "inlines.h"
#include "mnmain.h"
#include <melee/gm/gm_unsplit.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_013B.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lblanguage.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/cobj.h> // IWYU pragma: keep
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>

#define MAX_ITER 100000
#define NUM_STAGES 29

static struct StageListInfo {
    /* +00 */ HSD_JObj* x0;
    /* +04 */ int x4;
    /* +08 */ u8 x8;
    /* +09 */ u8 x9;
    /* +0A */ u8 xA;
    /* +0B */ u8 stkind;
    /* +0C */ f32 xC;
    /* +10 */ f32 x10;
    /* +14 */ f32 x14;
    /* +18 */ f32 x18;
} mnStageSel_803F06D0[30] = {
    { 0, 0, 0x2, 0x00, 0x00, 0x04, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x01, 0x0C, 0x0B, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x02, 0x01, 0x05, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x03, 0x0D, 0x0C, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x04, 0x02, 0x0D, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x05, 0x0E, 0x0E, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x08, 0x04, 0x08, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x09, 0x10, 0x10, 2.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0A, 0x05, 0x02, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0B, 0x11, 0x11, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0C, 0x06, 0x07, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0D, 0x12, 0x16, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x06, 0x03, 0x06, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x07, 0x0F, 0x0F, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x12, 0x09, 0x09, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x13, 0x15, 0x12, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x10, 0x08, 0x0A, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x11, 0x14, 0x18, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0E, 0x07, 0x03, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x0F, 0x13, 0x17, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x14, 0x0B, 0x13, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x15, 0x16, 0x14, 3.1F, 2.7F, 1.0F, 1.0F },
    { 0, 0, 0x2, 0x16, 0x0A, 0x19, 3.1F, 2.9F, 1.0F, 1.1F },
    { 0, 0, 0x2, 0x17, 0x17, 0x1B, 3.1F, 2.9F, 1.0F, 1.1F },
    { 0, 0, 0x2, 0x18, 0x18, 0x1F, 2.9F, 2.1F, 0.8F, 0.8F },
    { 0, 0, 0x2, 0x19, 0x19, 0x20, 2.9F, 2.1F, 0.8F, 0.8F },
    { 0, 0, 0x2, 0x1A, 0x1A, 0x1C, 2.9F, 2.1F, 0.8F, 0.8F },
    { 0, 0, 0x2, 0x1B, 0x1B, 0x1D, 2.9F, 2.1F, 0.8F, 0.8F },
    { 0, 0, 0x2, 0x1C, 0x1C, 0x1E, 2.9F, 2.1F, 0.8F, 0.8F },
    { 0, 0, 0x2, 0x1D, 0x00, 0x00, 3.6F, 2.7F, 1.2F, 1.0F },
};
ASSERT_SIZE(mnStageSel_803F06D0[0], 0x1C);


static s8 mnStageSel_804D50A0 = -1;

static SSSData* sss_data;
static HSD_Archive* mnStageSel_804D6C94;
static MnSelectStageModels* sss_models;
static HSD_GObj* mnStageSel_804D6C9C;
static u32 mnStageSel_804D6CA0;
static u32 mnStageSel_804D6CA4;
static s32 mnStageSel_804D6CA8;
static s8 mnStageSel_804D6CAC;
static s8 mnStageSel_804D6CAD;
static u8 mnStageSel_804D6CAE;
static u8 mnStageSel_804D6CAF;

struct StageSelUserData {
    u16 x0;
    u16 x2;
    int x4;
};

#ifdef TARGET_PC
#include "pc/net.h"
#include "pc/net_lan.h"
#include "pc/net_rank_session.h"
#include <melee/gm/gmonlinemode.h>
#include <sysdolphin/baselib/sislib.h>
#include "pc/pc.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <melee/gr/grace.h>
#include <melee/gr/grpstadium.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

/* A compact ranked list uses synchronized pads and makes both bans visible.
 * It does not depend on the archive's cursor geometry. */
static HSD_Text* rank_text;
static int rank_lines[7], rank_cursor, rank_axis;
static const unsigned rank_stages[] = {2, 3, 8, 28, 31, 32};
static const char* rank_names[] = {"FOUNTAIN OF DREAMS", "POKEMON STADIUM",
    "YOSHI'S STORY", "DREAM LAND", "BATTLEFIELD", "FINAL DESTINATION"};
static void rankStageDraw(void)
{
    int port = pc_rank_session_stage_port();
    HSD_SisLib_803A70A0(rank_text, rank_lines[0], "P%d %s", port + 1,
                       pc_rank_session_stage_prompt());
    for (int i = 0; i < 6; i++) {
        HSD_SisLib_803A70A0(rank_text, rank_lines[i + 1], "%s %s %s",
            i == rank_cursor ? ">" : " ", rank_names[i],
            pc_rank_session_stage_available(rank_stages[i]) ? "" : "(BANNED)");
    }
}
static void rankStageCreate(void)
{
    HSD_SisLib_803A62A0(0, lbLang_IsSavedLanguageUS() ? "SdMenu.usd" : "SdMenu.dat",
                       "SIS_MenuData");
    HSD_SisLib_803A611C(0, NULL, 9, 0xD, 0, 0xE, 0, 0x13);
    rank_text = HSD_SisLib_803A6754(0, 0);
    rank_text->default_kerning = 1;
    rank_cursor = rank_axis = 0;
    for (int i = 0; i < 7; i++) {
        rank_lines[i] = HSD_SisLib_803A6B98(rank_text, 50.0f, 100.0f + i * 40.0f, " ");
        HSD_SisLib_803A7548(rank_text, rank_lines[i], 0.55f, 0.55f);
    }
    rankStageDraw();
}
static void rankStageFrame(void)
{
    int port = pc_rank_session_stage_port();
    if (port < 0) return;
    HSD_PadStatus* pad = &HSD_PadCopyStatus[port];
    int axis = pad->stickY > 30 ? -1 : pad->stickY < -30 ? 1 : 0;
    int move = (pad->trigger & 8) ? -1 : (pad->trigger & 4) ? 1 :
               axis != rank_axis ? axis : 0;
    rank_axis = axis;
    if (move) rank_cursor = (rank_cursor + move + 6) % 6;
    if (pad->trigger & 0x200) {
        pc_rank_session_abort("ranked stage selection cancelled");
        gm_801A4B60();
        return;
    }
    if (pad->trigger & 0x1100) {
        if (!pc_rank_session_choose_stage(port, rank_stages[rank_cursor])) {
            lbAudioAx_80024030(3);
        } else if (pc_rank_session_stage()) {
            sss_data->vs.start.rules.stkind = pc_rank_session_stage();
            mnStageSel_804D6CAF = 2;
            gm_801A4B60();
            return;
        }
    }
    rankStageDraw();
}

/* Online (not ranked, which has its own list): one player picks the stage
 * for both -- P1 for a session's first game, then the loser of the last one
 * (gmOnline_StageChooser). Menus run on synced pads, so the chooser's cursor
 * moves and confirms on the same frame on both peers and nothing needs to be
 * exchanged. Cell values are table indices (mnStageSel_803F06D0), 30 =
 * random. */
static int net_chooser;
static u32 net_games; /* stage selects this session, so random varies */
static u32 net_games_session;

static bool netStageSel_Active(void)
{
    return pc_net_active();
}

static void netStageSel_Reset(void)
{
    if (net_games_session != pc_net_seed()) {
        net_games_session = pc_net_seed();
        net_games = 0;
    }
    net_games++;
    net_chooser = gmOnline_StageChooser();
    if (net_chooser < 0 || net_chooser > 1) {
        net_chooser = 0;
    }
    pc_log_line("sss: P%d picks the stage", net_chooser + 1);
}

/* Shared by both peers: the session seed, which game this is and the cell. */
static u32 netStageSel_Mix(void)
{
    return pc_net_seed() * 2654435761u + net_games * 40503u + (u32) mnStageSel_804D6CAE;
}

/* "Who picks" under the stage grid. */
static HSD_Text* net_text;
static void netStageSel_TextCreate(void)
{
    HSD_SisLib_803A62A0(0, lbLang_IsSavedLanguageUS() ? "SdMenu.usd" : "SdMenu.dat",
                       "SIS_MenuData");
    HSD_SisLib_803A611C(0, NULL, 9, 0xD, 0, 0xE, 0, 0x13);
    net_text = HSD_SisLib_803A6754(0, 0);
    net_text->default_kerning = 1;
    int line = HSD_SisLib_803A6B98(net_text, 40.0f, 30.0f, "P%d PICKS THE STAGE%s",
        net_chooser + 1, net_chooser == pc_net_local_player() ? " - YOU" : "");
    HSD_SisLib_803A7548(net_text, line, 0.5f, 0.5f);
}

/* "Random" online: the offline roll uses the live RNG and a per-machine
 * cooldown table, neither of which is in sync at this point (the peer's
 * pick lands on a different frame on each side). Draw from the seed
 * instead, over the stages the synced random-stage switches allow. */
static int ace_random_count(int* out);
static int pstadium_cell(void);

static int netStageSel_Random(void)
{
    int allowed[NUM_STAGES + GrAce_Count];
    int n = 0;
    int ace[GrAce_Count];
    int m, k;
    /* NUM_STAGES is 29 and the table holds 30: the last entry is the RANDOM
     * button, not a stage (StageListInfo table above, stkind 0). The bound
     * already excludes it; the stkind test says so out loud, because a
     * stkind of 0 starts a match with no stage that falls straight through
     * to the results screen, in sync, with nothing logged. */
    for (int i = 0; i < NUM_STAGES; i++) {
        if (mnStageSel_803F06D0[i].stkind != 0 &&
            (u8) gm_80164330(mnStageSel_803F06D0[i].xA)) {
            allowed[n++] = i;
        }
    }
    /* The ACE stages whose Random Stage switch is on (stage_mask, synced in
     * RULES; which are offered is the host's say, also synced): entries
     * past the cells, -1 - stage. */
    m = ace_random_count(ace);
    for (k = 0; k < m; k++) {
        allowed[n++] = -1 - ace[k];
    }
    grAce_Pick = GrAce_None;
    if (n == 0) {
        return 0;
    }
    u32 r = netStageSel_Mix() * 22695477u + 1u;
    int pick = allowed[(r >> 8) % (u32) n];
    if (pick < 0 && pstadium_cell() >= 0) {
        grAce_Pick = -1 - pick;
        pc_log_line("sss: random ACE stage %s", grAce_Name(grAce_Pick));
        return pstadium_cell();
    }
    return pick < 0 ? allowed[0] : pick;
}

/* The ACE stages "Random" may land on: offered here, their Random Stage
 * switch on (GRACE_SWITCH_BIT), not in a ranked session (no ACE tiles
 * there). Writes them to out and returns how many. */
static int ace_random_count(int* out)
{
    int k, m = 0;
    if (pc_rank_session_active() || pstadium_cell() < 0) {
        return 0;
    }
    for (k = GrAce_None + 1; k < GrAce_Count; k++) {
        if (grAce_Offered(k) &&
            gm_IsStageUnlocked((u16) GRACE_SWITCH_BIT(k)))
        {
            out[m++] = k;
        }
    }
    return m;
}

/// Pokemon Stadium's cell, which an ACE pick reports (as its tile does).
static int pstadium_cell(void)
{
    int i;
    for (i = 0; i < NUM_STAGES; i++) {
        if (mnStageSel_803F06D0[i].stkind == St_Kind_PStadium) {
            return i;
        }
    }
    return -1;
}

/* "Random" offline: the disc's roll (cooldowns and all) over the disc
 * stages, or, by the count of switches on, one of the ACE stages, which then
 * becomes ace_random. */
static int ace_random; /* GrAceStage the last offline Random landed on */
static int mnStageSel_RandomWithAce(void)
{
    int ace[GrAce_Count];
    int m = ace_random_count(ace);
    int n = 0, i;

    ace_random = GrAce_None;
    for (i = 0; i < NUM_STAGES; i++) {
        if (mnStageSel_803F06D0[i].stkind != 0 &&
            (u8) gm_80164330(mnStageSel_803F06D0[i].xA))
        {
            n++;
        }
    }
    if (m != 0) {
        int r = HSD_Randi(n + m);
        if (r >= n) {
            ace_random = ace[r - n];
            pc_log_line("sss: random ACE stage %s", grAce_Name(ace_random));
            return pstadium_cell();
        }
    }
    return mnStageSel_802599EC();
}
#endif

/// @todo .sdata2 order hack
#ifdef MUST_MATCH
static void order_sdata2(void)
{
    (void) S32_TO_F32;
}
#endif

/// Random stage selection
/// Returns an internal stage ID - 2 (since first 2 internal stage IDs are
/// invalid)
int mnStageSel_802599EC(void)
{
    int var_r0;
    int iter;
    bool var_r29 = true;
    int i;

    for (i = 0; i < NUM_STAGES; i++) {
        if (mnStageSel_803F06D0[i].x4 >= 0 &&
            (u8) gm_80164330(mnStageSel_803F06D0[i].xA))
        {
            break;
        }
    }
    if (i == NUM_STAGES) {
#ifdef TARGET_PC
        /* With only ACE switches on (or none offered), no disc stage passes
         * and the loop below would never end: Battlefield. */
        for (i = 0; i < NUM_STAGES; i++) {
            if ((u8) gm_80164330(mnStageSel_803F06D0[i].xA)) {
                break;
            }
        }
        if (i == NUM_STAGES) {
            for (i = 0; i < NUM_STAGES; i++) {
                if (mnStageSel_803F06D0[i].stkind == St_Kind_Battle) {
                    return i;
                }
            }
            return 0;
        }
#endif
        for (i = 0; i < NUM_STAGES; i++) {
            mnStageSel_803F06D0[i].x4 = 0;
        }
    }
    while (var_r29) {
        for (i = 0; i < NUM_STAGES; i++) {
            if (mnStageSel_803F06D0[i].x4 > 0) {
                mnStageSel_803F06D0[i].x4--;
            }
        }
        for (i = 0; i < NUM_STAGES; i++) {
            if (mnStageSel_803F06D0[i].x4 == 0 &&
                (u8) gm_80164330(mnStageSel_803F06D0[i].xA))
            {
                var_r29 = false;
            }
        }
    }
    for (iter = 0; iter < MAX_ITER; iter++) {
        int tmp = HSD_Randi(NUM_STAGES);
        i = tmp;
        if (mnStageSel_803F06D0[i].x4 == 0) {
            if ((u8) gm_80164330(mnStageSel_803F06D0[i].xA)) {
                break;
            }
        }
    }
    if (iter >= MAX_ITER) {
        i = 0;
    }
    mnStageSel_803F06D0[i].x4 = -1;
    if (i < 22) {
        if (i & 1) {
            var_r0 = i - 1;
        } else {
            var_r0 = i + 1;
        }
        if (mnStageSel_803F06D0[var_r0].x4 >= 0) {
            mnStageSel_803F06D0[var_r0].x4 = 3;
        }
    }
    return i;
}

#ifdef TARGET_PC
static inline void make_stage_icon(HSD_JObj** out);

/* A second Pokémon Stadium tile under the grid, tagged FROZEN: picking it
 * starts this match's Stadium with no transformations
 * (grStadium_FrozenPick). It reports Stadium's cell as the hovered one, so
 * the name, preview and pick all work as for Stadium; frozen_hover says
 * which of the two tiles the cursor is on. */
static int frozen_src = -1;      /* Pokémon Stadium's cell */
static HSD_JObj* frozen_src_root; /* the icon pair Stadium is drawn in */
static HSD_JObj* frozen_root;    /* the copy of that pair */
static HSD_JObj* frozen_tile;    /* the copy's Stadium half */
static HSD_Text* frozen_text;
static bool frozen_hover;
static int name_ace; /* the ACE stage the name card was last made for */

static bool frozen_shown(void)
{
    return frozen_tile != NULL && frozen_src >= 0 &&
           mnStageSel_803F06D0[frozen_src].x8 >= 2;
}

/* Every frame: one row under the lowest tile, in Stadium's column (the
 * grid slides in on entry, so this follows it). */
static void frozen_place(HSD_GObj* gobj)
{
    struct StageListInfo* src;
    Vec3 root, at, p;
    f32 lowest = 1e9f;
    int i;

    (void) gobj;
    if (frozen_tile == NULL) {
        return;
    }

    src = &mnStageSel_803F06D0[frozen_src];
    for (i = 0; i < 0x1E; i++) {
        if (mnStageSel_803F06D0[i].x8 == 0 || mnStageSel_803F06D0[i].x0 == NULL) {
            continue;
        }
        lb_8000B1CC(mnStageSel_803F06D0[i].x0, NULL, &p);
        if (p.y - mnStageSel_803F06D0[i].x10 < lowest) {
            lowest = p.y - mnStageSel_803F06D0[i].x10;
        }
    }
    lb_8000B1CC(frozen_src_root, NULL, &root);
    lb_8000B1CC(src->x0, NULL, &at);
    root.y += (lowest - 0.6f - src->x10) - at.y;
    HSD_JObjSetTranslate(frozen_root, &root);

    lb_8000B1CC(frozen_tile, NULL, &p);
    if (frozen_text != NULL) {
        /* Text y runs down the screen (the opposite of world y): this puts
         * the tag's top edge just under the tile. */
        frozen_text->pos_x = p.x - src->xC;
        frozen_text->pos_y = -(p.y - src->x10) + 0.2f;
        frozen_text->pos_z = p.z + 0.5f;
    }
}

static void frozen_create(void)
{
    struct StageListInfo* src;
    HSD_JObj* other;
    HSD_GObj* gobj;
    int i, canvas;

    frozen_src = -1;
    for (i = 0; i < 0x16; i++) {
        if (mnStageSel_803F06D0[i].stkind == St_Kind_PStadium) {
            frozen_src = i;
        }
    }
    if (frozen_src < 0 || pc_rank_session_active()) {
        return;
    }
    src = &mnStageSel_803F06D0[frozen_src];
    if (src->x8 < 2 || src->x0 == NULL || src->x0->parent == NULL) {
        return; /* Stadium locked: no frozen copy either */
    }
    frozen_src_root = src->x0->parent;
    make_stage_icon(&frozen_root);
    /* Even cells are a pair's second half (child->next), odd the first. */
    if (frozen_src & 1) {
        frozen_tile = frozen_root->child;
        other = frozen_root->child->next;
    } else {
        frozen_tile = frozen_root->child->next;
        other = frozen_root->child;
    }
    HSD_JObjSetFlags(other, JOBJ_HIDDEN);
    HSD_JObjReqAnimAllByFlags(frozen_tile, 0x10, src->x9 / 2 + 2);
    HSD_JObjAnimAll(frozen_root);
    HSD_ForeachAnim(frozen_root, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                    AOBJ_ARG_AOV, NULL);
    HSD_JObjSetTranslateX(frozen_root, 100.0F); /* placed from the next frame */

    gobj = GObj_Create(4, 5, 0x80);
    HSD_GObj_SetupProc(gobj, frozen_place, 3);

    /* The tag, drawn by the stage select camera just above the icons. */
    if (!netStageSel_Active()) {
        HSD_SisLib_803A62A0(0, lbLang_IsSavedLanguageUS() ? "SdMenu.usd" : "SdMenu.dat",
                           "SIS_MenuData");
    }
    canvas = HSD_SisLib_803A611C(0, mnStageSel_804D6C9C, 7, 8, 0x80, 4, 0x84, 0);
    frozen_text = HSD_SisLib_803A6754(0, canvas);
    if (frozen_text != NULL) {
        f32 font = 0.035f;
        frozen_text->pos_x = 100.0f;
        frozen_text->box_size_x = 2.0f * src->xC / font;
        frozen_text->box_size_y = 30.0f;
        frozen_text->font_size.x = font;
        frozen_text->font_size.y = font;
        frozen_text->default_fitting = 1;
        frozen_text->default_alignment = 1;
        frozen_text->default_kerning = 1;
        frozen_text->x4C = 1;
        frozen_text->bg_color.r = 0x10;
        frozen_text->bg_color.g = 0x30;
        frozen_text->bg_color.b = 0x60;
        frozen_text->bg_color.a = 0xC0;
        HSD_SisLib_803A6B98(frozen_text, src->xC / font, 0.0f, "FROZEN");
    }
}

/* The ACE stages (gr/grace.h): a tile each, in the FROZEN tile's row next to
 * it, showing ACE's own icon. Like the FROZEN tile, hovering one reports
 * Stadium's cell (so a pick passes every check a normal pick does) and
 * ace_hover says which ACE stage it is; the name and preview, which are
 * Stadium's, are hidden meanwhile. The pick becomes grAce_Pick and the
 * match's stage St_Kind 0x35 (GRACE_STKIND). */
static HSD_JObj* ace_root[GrAce_Count];
static HSD_JObj* ace_tile[GrAce_Count];
static HSD_Text* ace_text[GrAce_Count];
static int ace_hover; /* GrAceStage under the cursor, or GrAce_None */

static bool ace_shown(int k)
{
    return ace_tile[k] != NULL && frozen_src >= 0 &&
           mnStageSel_803F06D0[frozen_src].x8 >= 2;
}

static bool ace_hovered(void)
{
    return ace_hover != GrAce_None && ace_shown(ace_hover) &&
           mnStageSel_804D6CAE == frozen_src;
}

/* An ACE .tex (MexManager's icon format): a 32-byte header -- "TEX\0",
 * GXTexFmt, GXTlutFmt, then little-endian u32s: width, height, the image's
 * offset and size, the palette's offset and size -- and the GX data as the
 * GameCube reads it. */
static bool ace_load_icon(int k, HSD_ImageDesc** out_desc, HSD_Tlut** out_tlut)
{
    char path[1024];
    u8 hdr[32];
    u32 w, h, img_off, img_size, pal_off, pal_size;
    u8* img;
    u8* pal = NULL;
    HSD_ImageDesc* desc;
    HSD_Tlut* tlut = NULL;
    FILE* f;
    long len;

    if (pc_ace_dir()[0] == '\0' || grAce_IconFile(k) == NULL) {
        return false;
    }
    snprintf(path, sizeof path, "%s/%s", pc_ace_dir(), grAce_IconFile(k));
    f = fopen(path, "rb");
    if (f == NULL) {
        pc_log_line("sss: no ACE icon %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 32 || fread(hdr, 1, 32, f) != 32 || memcmp(hdr, "TEX", 4) != 0) {
        fclose(f);
        pc_log_line("sss: %s is not an ACE icon", path);
        return false;
    }
#define LE32(p) ((u32) (p)[3] << 24 | (u32) (p)[2] << 16 | (u32) (p)[1] << 8 | (p)[0])
    w = LE32(hdr + 8);
    h = LE32(hdr + 12);
    img_off = LE32(hdr + 16);
    img_size = LE32(hdr + 20);
    pal_off = LE32(hdr + 24);
    pal_size = LE32(hdr + 28);
#undef LE32
    if (w == 0 || h == 0 || w > 1024 || h > 1024 ||
        (long) img_off + (long) img_size > len ||
        (long) pal_off + (long) pal_size > len || pal_size > 0x2000)
    {
        fclose(f);
        pc_log_line("sss: %s has a bad header", path);
        return false;
    }
    img = HSD_MemAlloc(img_size);
    if (pal_size != 0) {
        pal = HSD_MemAlloc(pal_size);
    }
    if (fseek(f, img_off, SEEK_SET) != 0 || fread(img, 1, img_size, f) != img_size ||
        (pal != NULL && (fseek(f, pal_off, SEEK_SET) != 0 ||
                         fread(pal, 1, pal_size, f) != pal_size)))
    {
        fclose(f);
        pc_log_line("sss: could not read %s", path);
        return false;
    }
    fclose(f);

    desc = HSD_MemAlloc(sizeof(*desc));
    memset(desc, 0, sizeof(*desc));
    DP_SET(desc->image_ptr, img);
    desc->width = (u16) w;
    desc->height = (u16) h;
    desc->format = (GXTexFmt) hdr[4];
    desc->mipmap = 0;
    desc->minLOD = 0.0f;
    desc->maxLOD = 0.0f;
    if (pal != NULL) {
        tlut = HSD_MemAlloc(sizeof(*tlut));
        memset(tlut, 0, sizeof(*tlut));
        tlut->lut = pal;
        tlut->fmt = (GXTlutFmt) hdr[5];
        tlut->n_entries = (u16) (pal_size / 2);
    }
    *out_desc = desc;
    *out_tlut = tlut;
    return true;
}

/* Put the icon on every animated texture of the tile (the stage picture:
 * the grid's tiles pick theirs by animation frame), whose animations
 * frozen-style creation has already stopped. */
static int ace_retexture(HSD_JObj* jobj, HSD_ImageDesc* desc, HSD_Tlut* tlut,
                         bool siblings)
{
    int n = 0;
    for (; jobj != NULL; jobj = siblings ? jobj->next : NULL) {
        HSD_DObj* dobj = HSD_JObjGetDObj(jobj);
        for (; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* tobj = dobj->mobj != NULL ? dobj->mobj->tobj : NULL;
            for (; tobj != NULL; tobj = tobj->next) {
                if (tobj->imagetbl == NULL) {
                    continue;
                }
                tobj->imagedesc = desc;
                tobj->tlut = tlut;
                tobj->tlut_no = (u8) -1;
                n++;
            }
        }
        n += ace_retexture(HSD_JObjGetChild(jobj), desc, tlut, true);
        if (!siblings) {
            break;
        }
    }
    return n;
}

/* Every frame, after frozen_place: the FROZEN tile's row, one tile further
 * out each -- to the right of it, or to its left when the grid's right edge
 * is too close. */
static void ace_place(HSD_GObj* gobj)
{
    struct StageListInfo* src;
    Vec3 base, p;
    f32 right = -1e9f, step, dir;
    int k, slot, i;

    (void) gobj;
    if (frozen_src < 0 || frozen_root == NULL) {
        return;
    }
    src = &mnStageSel_803F06D0[frozen_src];
    for (i = 0; i < 0x1E; i++) {
        if (mnStageSel_803F06D0[i].x8 == 0 || mnStageSel_803F06D0[i].x0 == NULL) {
            continue;
        }
        lb_8000B1CC(mnStageSel_803F06D0[i].x0, NULL, &p);
        if (p.x + mnStageSel_803F06D0[i].xC > right) {
            right = p.x + mnStageSel_803F06D0[i].xC;
        }
    }
    HSD_JObjGetTranslation(frozen_root, &base);
    lb_8000B1CC(frozen_tile, NULL, &p);
    step = 2.0f * src->xC + 0.6f;
    dir = (p.x + 3.0f * step + src->xC <= right + 0.5f) ? 1.0f : -1.0f;

    /* Reading order stays Metal Cavern, WarioWare, Colosseum either way. */
    slot = 0;
    for (k = GrAce_None + 1; k < GrAce_Count; k++) {
        slot += ace_root[k] != NULL;
    }
    i = 0;
    for (k = GrAce_None + 1; k < GrAce_Count; k++) {
        Vec3 at = base;
        if (ace_root[k] == NULL) {
            continue;
        }
        i++; /* 1..slot, left to right */
        at.x += (dir > 0.0f ? i : -(slot + 1 - i)) * step;
        HSD_JObjSetTranslate(ace_root[k], &at);
        if (ace_text[k] != NULL) {
            Vec3 t;
            lb_8000B1CC(ace_tile[k], NULL, &t);
            ace_text[k]->pos_x = t.x - src->xC;
            ace_text[k]->pos_y = -(t.y - src->x10) + 0.2f;
            ace_text[k]->pos_z = t.z + 0.5f;
        }
    }
}

static void ace_create(void)
{
    struct StageListInfo* src;
    HSD_GObj* gobj;
    int k, canvas, made = 0;

    for (k = 0; k < GrAce_Count; k++) {
        ace_root[k] = ace_tile[k] = NULL;
        ace_text[k] = NULL;
    }
    ace_hover = GrAce_None;
    /* Placed in the FROZEN tile's row: no row without it. */
    if (frozen_root == NULL || pc_rank_session_active()) {
        return;
    }
    src = &mnStageSel_803F06D0[frozen_src];
    for (k = GrAce_None + 1; k < GrAce_Count; k++) {
        HSD_ImageDesc* desc;
        HSD_Tlut* tlut;
        HSD_JObj* other;
        int n;

        if (!grAce_Offered(k)) {
            continue;
        }
        if (!ace_load_icon(k, &desc, &tlut)) {
            desc = NULL; /* still offered: Stadium's picture, named below */
        }
        make_stage_icon(&ace_root[k]);
        if (frozen_src & 1) {
            ace_tile[k] = ace_root[k]->child;
            other = ace_root[k]->child->next;
        } else {
            ace_tile[k] = ace_root[k]->child->next;
            other = ace_root[k]->child;
        }
        HSD_JObjSetFlags(other, JOBJ_HIDDEN);
        HSD_JObjReqAnimAllByFlags(ace_tile[k], 0x10, src->x9 / 2 + 2);
        HSD_JObjAnimAll(ace_root[k]);
        HSD_ForeachAnim(ace_root[k], JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                        AOBJ_ARG_AOV, NULL);
        HSD_JObjSetTranslateX(ace_root[k], 100.0F); /* placed from the next frame */
        n = desc != NULL ? ace_retexture(ace_tile[k], desc, tlut, false) : 0;
        pc_log_line("sss: ACE tile %s (%d textures from %s)", grAce_Name(k), n,
                    desc != NULL ? grAce_IconFile(k) : "nothing");

        /* A canvas each, as the FROZEN tag has: the tag's backing box only
         * showed for a canvas's first text. */
        canvas = HSD_SisLib_803A611C(0, mnStageSel_804D6C9C, 7, 8, 0x80, 4, 0x84, 0);
        ace_text[k] = HSD_SisLib_803A6754(0, canvas);
        if (ace_text[k] != NULL) {
            HSD_Text* t = ace_text[k];
            f32 font = 0.035f;
            t->pos_x = 100.0f;
            t->box_size_x = 2.0f * src->xC / font;
            t->box_size_y = 30.0f;
            t->font_size.x = font;
            t->font_size.y = font;
            t->default_fitting = 1;
            t->default_alignment = 1;
            t->default_kerning = 1;
            t->x4C = 1;
            t->bg_color.r = 0x10;
            t->bg_color.g = 0x30;
            t->bg_color.b = 0x60;
            t->bg_color.a = 0xC0;
            char tag[32];
            size_t i;
            for (i = 0; grAce_Name(k)[i] != '\0' && i + 1 < sizeof tag; i++) {
                tag[i] = (char) toupper((unsigned char) grAce_Name(k)[i]);
            }
            tag[i] = '\0';
            HSD_SisLib_803A6B98(t, src->xC / font, 0.0f, tag);
        }
        made++;
    }
    if (made != 0) {
        /* After frozen_place (same priority, created later). */
        gobj = GObj_Create(4, 5, 0x80);
        HSD_GObj_SetupProc(gobj, ace_place, 3);
    }
}
#endif

void mnStageSel_80259C28(void)
{
    HSD_JObj* jobj;
    HSD_GObj* gobj;
    u64 _[2];

#ifdef TARGET_PC
    if (rank_text) return;
#endif
    if (mnStageSel_804D6CA4 != 0) {
        return;
    }
    switch (mnStageSel_804D6CAE) {
    case 30:
        if (!(mnStageSel_804D6CA0 & 0x1000)) {
            return;
        }
        break;
    case 29:
        if (!(mnStageSel_804D6CA0 & 0x1100)) {
            return;
        }
        break;
    default:
        if (!(mnStageSel_804D6CA0 & 0x1100)) {
            return;
        }
        if (mnStageSel_804D6CAE < 0x1E &&
            mnStageSel_803F06D0[mnStageSel_804D6CAE].x8 >= 2)
        {
            goto skip_randomize;
        }
        lbAudioAx_80024030(3);
        return;
    }
#ifdef TARGET_PC
    if (netStageSel_Active() && !pc_rank_session_active()) {
        /* Defer the roll to the resolve, which draws from the shared seed:
         * the live RNG's cooldown table differs per machine. */
        goto skip_randomize;
    }
    mnStageSel_804D6CAE = mnStageSel_RandomWithAce();
#else
    mnStageSel_804D6CAE = mnStageSel_802599EC();
#endif
skip_randomize:
#ifdef TARGET_PC
    grStadium_FrozenPick = frozen_hover && frozen_shown() &&
                           mnStageSel_804D6CAE == frozen_src;
    if (grStadium_FrozenPick) {
        pc_log_line("sss: Pokemon Stadium picked frozen");
    }
    grAce_Pick = ace_hovered() ? ace_hover : GrAce_None;
    if (ace_random != GrAce_None) {
        grAce_Pick = ace_random;
        grStadium_FrozenPick = false;
        ace_random = GrAce_None;
    }
    if (grAce_Pick != GrAce_None) {
        pc_log_line("sss: ACE stage %s picked", grAce_Name(grAce_Pick));
    }
#endif

    gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 5, 0x80);
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->now_loading.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x87);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 0);
    HSD_JObjAddAnimAll(jobj, DP(HSD_AnimJoint, sss_models->now_loading.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->now_loading.matanim_joint), DP(HSD_ShapeAnimJoint,
                          sss_models->now_loading.shapeanim_joint));
    HSD_JObjReqAnimAll(gobj->hsd_obj, 0.0F);
    HSD_JObjAnimAll(gobj->hsd_obj);
    mnStageSel_804D6CAF = 1;
    mnStageSel_804D6CA4 = 0x1E;
    sfxForward();
}

void fn_80259D84(HSD_GObj* gobj)
{
    struct StageSelUserData* temp_r31 = HSD_GObjGetUserData(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);

    switch (temp_r31->x2) {
    case 0:
        if (mnStageSel_804D6CAF == 0) {
            mnStageSel_80259C28();
        }
        if (++temp_r31->x4 >= 9U) {
            HSD_ForeachAnim(jobj, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                            AOBJ_ARG_AOV, NULL);
            temp_r31->x2 = 1;
        }
        break;
    case 1:
        if (mnStageSel_804D6CAF == 0) {
            mnStageSel_80259C28();
        }
        if (temp_r31->x0 != mnStageSel_804D6CAE
#ifdef TARGET_PC
            || name_ace != (ace_hovered() ? ace_hover : GrAce_None)
#endif
        )
        {
            if (temp_r31->x0 < 0x1E &&
                mnStageSel_803F06D0[temp_r31->x0].x8 >= 2)
            {
                HSD_JObjReqAnimAllByFlags(jobj, 1, 10.0F);
            }
            sfxMove();
            temp_r31->x4 = 0;
            temp_r31->x2 = 2;
            mnStageSel_80259ED8(mnStageSel_804D6CAE);
        }
        break;
    case 2:
        if (++temp_r31->x4 > 0xAU) {
            HSD_GObjFree(gobj);
            temp_r31->x2++;
        }
        break;
    }
}

static void do_anim(HSD_JObj* jobj, int frame)
{
    HSD_JObjReqAnimAll(jobj, 0.0F);
    HSD_JObjReqAnimAllByFlags(jobj, 0x10, frame);
    HSD_JObjAnimAll(jobj);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV,
                    NULL);
}

void mnStageSel_80259ED8(int id)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    struct StageSelUserData* temp_r3_2;
    u8 _[4];

    gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 5, 0x80);
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->stage_name.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x84);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 1);
    HSD_JObjAddAnimAll(jobj,
                       DP(HSD_AnimJoint, sss_models->stage_name.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->stage_name.matanim_joint),
                       DP(HSD_ShapeAnimJoint,
                          sss_models->stage_name.shapeanim_joint));
    jobj = GET_JOBJ(gobj);
    temp_r3_2 = HSD_MemAlloc(sizeof(struct StageSelUserData));
    GObj_InitUserData(gobj, 4, HSD_Free, temp_r3_2);
    HSD_GObj_SetupProc(gobj, fn_80259D84, 1);
    temp_r3_2->x0 = id;
    temp_r3_2->x4 = 0;
    temp_r3_2->x2 = 0;
#ifdef TARGET_PC
    /* An ACE tile has no name card of its own (its tag says it). */
    name_ace = ace_hovered() ? ace_hover : GrAce_None;
    if (name_ace != GrAce_None) {
        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        return;
    }
#endif
    if (id < 0x1E && mnStageSel_803F06D0[id].x8 >= 2) {
        do_anim(jobj, 20.0F * mnStageSel_803F06D0[id].x9);
    }
}

struct StagePreviewUserData {
    u32 x0;
    u32 x4;
};

void fn_8025A090(HSD_GObj* gobj)
{
    u32 var_r3;
    struct StagePreviewUserData* temp_r30;
    HSD_JObj* jobj;

    jobj = GET_JOBJ(gobj);
    temp_r30 = HSD_GObjGetUserData(gobj);
    var_r3 = mnStageSel_804D6CAE;
    /* 0x1E is the "random" sentinel (see :485), one past the end of the
     * 30-entry table. */
    if (mnStageSel_804D6CAE >= 0x1E ||
        mnStageSel_803F06D0[mnStageSel_804D6CAE].x8 < 2)
    {
        var_r3 = 0x1E;
    }
#ifdef TARGET_PC
    if (ace_hovered()) {
        var_r3 = 0x1E; /* no preview: it would be Stadium's */
    }
#endif
    if (temp_r30->x0 != var_r3) {
        temp_r30->x0 = var_r3;
        temp_r30->x4 = 0;
        if ((s32) var_r3 < 0x1D) {
            HSD_JObjReqAnimAll(jobj, 50.0F * mnStageSel_803F06D0[var_r3].x9);
            HSD_JObjAnimAll(jobj);
            HSD_ForeachAnim(jobj, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                            AOBJ_ARG_AOV, NULL);
            HSD_JObjSetTranslateX(jobj, 0.0F);
        } else {
            HSD_JObjSetTranslateX(jobj, 100.0F);
        }
    }
    if (temp_r30->x4 < 0x5A) {
        temp_r30->x4++;
        if (temp_r30->x4 == 0x14 && temp_r30->x0 < 0x1E) {
            HSD_JObjReqAnimAll(jobj,
                               50.0F * mnStageSel_803F06D0[temp_r30->x0].x9);
        }
        if (temp_r30->x4 == 0x45) {
            HSD_ForeachAnim(jobj, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim,
                            AOBJ_ARG_AOV, NULL);
        }
    } else if (mnStageSel_804D6CAF) {
        mnStageSel_804D6CAF = 2;
    }
}

void fn_8025A310(HSD_GObj* gobj)
{
    Vec3 sp1C;
    Vec3 sp10;
    f32 temp_f1;
    f32 temp_f2;
    int i;
    HSD_JObj* jobj;
    u32 unused;

    jobj = gobj->hsd_obj;
    if (mnStageSel_804D6CAF != 0) {
        HSD_JObjSetFlags(jobj, JOBJ_HIDDEN);
        return;
    }
    HSD_JObjGetTranslation(jobj, &sp1C);
    sp1C.x = 0.03f * mnStageSel_804D6CAC + sp1C.x;
    if (-27.0F > sp1C.x) {
        sp1C.x = -27.0F;
    }
    if (27.0F < sp1C.x) {
        sp1C.x = 27.0F;
    }
    sp1C.y = 0.03f * mnStageSel_804D6CAD + sp1C.y;
    if (-19.0F > sp1C.y) {
        sp1C.y = -19.0F;
    }
    if (19.0F < sp1C.y) {
        sp1C.y = 19.0F;
    }

    HSD_JObjSetTranslate(jobj, &sp1C);
    lb_8000B1CC(jobj, NULL, &sp1C);
#ifdef TARGET_PC
    if (frozen_shown()) {
        struct StageListInfo* src = &mnStageSel_803F06D0[frozen_src];
        lb_8000B1CC(frozen_tile, NULL, &sp10);
        if (sp10.x - src->xC < sp1C.x && sp10.x + src->xC > sp1C.x &&
            sp10.y - src->x10 < sp1C.y && sp10.y + src->x10 > sp1C.y)
        {
            mnStageSel_804D6CAE = frozen_src;
            frozen_hover = true;
            ace_hover = GrAce_None;
            return;
        }
    }
    {
        int k;
        for (k = GrAce_None + 1; k < GrAce_Count; k++) {
            struct StageListInfo* src;
            if (!ace_shown(k)) {
                continue;
            }
            src = &mnStageSel_803F06D0[frozen_src];
            lb_8000B1CC(ace_tile[k], NULL, &sp10);
            if (sp10.x - src->xC < sp1C.x && sp10.x + src->xC > sp1C.x &&
                sp10.y - src->x10 < sp1C.y && sp10.y + src->x10 > sp1C.y)
            {
                mnStageSel_804D6CAE = frozen_src;
                frozen_hover = false;
                ace_hover = k;
                return;
            }
        }
    }
#endif
    for (i = 0; i < 0x1E; i++) {
        if (mnStageSel_803F06D0[i].x8 != 0) {
            lb_8000B1CC(mnStageSel_803F06D0[i].x0, NULL, &sp10);
            temp_f2 = sp10.x;
            temp_f1 = mnStageSel_803F06D0[i].xC;
            if (temp_f2 - temp_f1 < sp1C.x && temp_f2 + temp_f1 > sp1C.x) {
                if (sp10.y - mnStageSel_803F06D0[i].x10 < sp1C.y &&
                    sp10.y + mnStageSel_803F06D0[i].x10 > sp1C.y)
                {
                    mnStageSel_804D6CAE = i;
#ifdef TARGET_PC
                    frozen_hover = false;
                    ace_hover = GrAce_None;
#endif
                    return;
                }
            }
        }
    }
}

void fn_8025A560(HSD_GObj* gobj)
{
    s32* temp_r30;
    Vec3 sp10;
    HSD_JObj* jobj = GET_JOBJ(gobj);
    temp_r30 = HSD_GObjGetUserData(gobj);

#ifdef TARGET_PC
    if (ace_hovered()) {
        lb_8000B1CC(ace_tile[ace_hover], NULL, &sp10);
        HSD_JObjSetTranslateX(jobj, sp10.x);
        HSD_JObjSetTranslateY(jobj, sp10.y);
        HSD_JObjSetScaleX(jobj, mnStageSel_803F06D0[frozen_src].x14);
        HSD_JObjSetScaleY(jobj, mnStageSel_803F06D0[frozen_src].x18);
    } else if (frozen_hover && frozen_shown() && mnStageSel_804D6CAE == frozen_src) {
        lb_8000B1CC(frozen_tile, NULL, &sp10);
        HSD_JObjSetTranslateX(jobj, sp10.x);
        HSD_JObjSetTranslateY(jobj, sp10.y);
        HSD_JObjSetScaleX(jobj, mnStageSel_803F06D0[frozen_src].x14);
        HSD_JObjSetScaleY(jobj, mnStageSel_803F06D0[frozen_src].x18);
    } else
#endif
    if (mnStageSel_804D6CAE < 0x16 && (mnStageSel_804D6CAE & 1) &&
        mnStageSel_803F06D0[mnStageSel_804D6CAE].x8 == 0)
    {
        HSD_JObjSetTranslateX(jobj, 100.0F);
    } else if (mnStageSel_804D6CAE < 0x1E) {
        lb_8000B1CC(mnStageSel_803F06D0[mnStageSel_804D6CAE].x0, NULL, &sp10);
        HSD_JObjSetTranslateX(jobj, sp10.x);
        HSD_JObjSetTranslateY(jobj, sp10.y);
        HSD_JObjSetScaleX(jobj, mnStageSel_803F06D0[mnStageSel_804D6CAE].x14);
        HSD_JObjSetScaleY(jobj, mnStageSel_803F06D0[mnStageSel_804D6CAE].x18);
    } else {
        HSD_JObjSetTranslateX(jobj, 100.0F);
    }
    if (mnStageSel_804D6CAF != 0) {
        HSD_JObjReqAnimAll(jobj, 0.0F);
        HSD_JObjAnimAll(jobj);
        return;
    }
    if (++*temp_r30 >= 10) {
        *temp_r30 = 0;
        HSD_JObjReqAnimAll(jobj, 0.0F);
        HSD_JObjAnimAll(jobj);
    }
}

void fn_8025A91C(HSD_GObj* gobj)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    if (++mnStageSel_804D6CA8 >= 0xFA) {
        mnStageSel_804D6CA8 = 0;
        HSD_JObjReqAnimAll(jobj, 0.0F);
        HSD_JObjAnimAll(jobj);
    }
}

void fn_8025A974(HSD_GObj* gobj, intptr_t unused)
{
    HSD_FogSet(gobj->hsd_obj);
}

static const Vec3 mnStageSel_803B8550 = { 0, -13, 0 };

static inline void make_stage_icon(HSD_JObj** out)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    gobj = GObj_Create(4, 5, 0x80);
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->icon_stacked.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x83);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 3);
    HSD_JObjAddAnimAll(jobj,
                       DP(HSD_AnimJoint, sss_models->icon_stacked.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->icon_stacked.matanim_joint),
                       DP(HSD_ShapeAnimJoint,
                          sss_models->icon_stacked.shapeanim_joint));
    *out = GET_JOBJ(gobj);
}

static inline void attach_menu_model(HSD_GObj* gobj)
{
    HSD_JObj* jobj;
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->background.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x80);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 0);
    HSD_JObjAddAnimAll(jobj,
                       DP(HSD_AnimJoint, sss_models->background.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->background.matanim_joint),
                       DP(HSD_ShapeAnimJoint,
                          sss_models->background.shapeanim_joint));
}

static inline void make_bg_model(HSD_JObj** out)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    gobj = GObj_Create(4, 5, 0x80);
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->menu_border.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x82);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 0);
    HSD_JObjAddAnimAll(jobj,
                       DP(HSD_AnimJoint, sss_models->menu_border.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->menu_border.matanim_joint),
                       DP(HSD_ShapeAnimJoint,
                          sss_models->menu_border.shapeanim_joint));
    *out = gobj->hsd_obj;
}

static inline void make_icon_root(HSD_JObj** icons)
{
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    gobj = GObj_Create(4, 5, 0x80);
    jobj = HSD_JObjLoadJoint(DP(HSD_Joint, sss_models->layout.joint));
    HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x82);
    HSD_GObj_SetupProc(gobj, mn_8022EAE0, 4);
    HSD_JObjAddAnimAll(jobj,
                       DP(HSD_AnimJoint, sss_models->layout.animjoint),
                       DP(HSD_MatAnimJoint,
                          sss_models->layout.matanim_joint),
                       DP(HSD_ShapeAnimJoint,
                          sss_models->layout.shapeanim_joint));
    icons[0] = GET_JOBJ(gobj)->child;
    HSD_JObjReqAnimAll(icons[0], 0.0F);
    HSD_JObjAnimAll(icons[0]);
}

static inline HSD_JObj* get_jobj(HSD_GObj* gobj)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    return jobj;
}

void mnStageSel_Scene_OnEnter(void* arg0)
{
#ifdef TARGET_PC
    grStadium_FrozenPick = false; /* until the FROZEN tile is picked */
    grAce_Pick = GrAce_None;      /* until an ACE tile is */
    ace_hover = GrAce_None;
    name_ace = GrAce_None;
    frozen_hover = false;
    frozen_src = -1;
    frozen_root = frozen_tile = NULL;
    frozen_text = NULL;
#endif
    HSD_JObj* spDC[0x13];
    u8 _[0xDC - 0xD8];
    Vec3 spCC;

    int i;
    struct MnSelectStageDataTable* sss_data_table;

    PAD_STACK(0xDC - 0x50);

    sss_data = (SSSData*) arg0;

    if (sss_data->force_stage_id < 0) {
        if (lbLang_IsSavedLanguageUS() != 0) {
            mnStageSel_804D6C94 = lbArchive_LoadArchive("MnSlMap.usd");
        } else {
            mnStageSel_804D6C94 = lbArchive_LoadArchive("MnSlMap.dat");
        }
        sss_data_table = HSD_ArchiveGetPublicAddress(mnStageSel_804D6C94,
                                                     "MnSelectStageDataTable");
        MenMain_cam = DP(HSD_CObjDesc, sss_data_table->cam);
        sss_models = &sss_data_table->models;
        mnStageSel_804D6CAF = 0;
        mnStageSel_804D6CA0 = 0;
        mnStageSel_804D6CAC = 0;
        mnStageSel_804D6CAD = 0;
        mnStageSel_804D6CAE = 0x1E;
        mnStageSel_804D50A0 = sss_data->unk_stage - 1;
#ifdef TARGET_PC
        if (netStageSel_Active()) {
            if (!pc_rank_session_active()) {
                netStageSel_Reset();
                mnStageSel_804D50A0 = net_chooser;
            } else {
                mnStageSel_804D50A0 = -1;
            }
        }
#endif
        mnStageSel_804D6CA4 = 0x14;

        {
            HSD_GObj* gobj = mnStageSel_804D6C9C = GObj_Create(2, 3, 0x80);
            HSD_CObj* cobj = HSD_CObjLoadDesc(MenMain_cam);
            HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind, cobj);
            GObj_SetupGXLinkMax(gobj, HSD_GObj_803910D8, 0);
            gobj->gxlink_prios = 0x11;
            HSD_GObj_SetupProc(gobj, mn_8022BA1C, 5);
        }

        {
            HSD_GObj* gobj;
            HSD_LObj* lobj1;
            HSD_LObj* lobj2;
            gobj = GObj_Create(3, 4, 0x80);
            lobj1 = HSD_LObjLoadDesc(DP(HSD_LightDesc, sss_data_table->light0));
            lobj2 = HSD_LObjLoadDesc(DP(HSD_LightDesc, sss_data_table->light1));
            HSD_LObjSetNext(lobj1, lobj2);
            HSD_GObjObject_80390A70(gobj, (u8) HSD_GObj_LightKind, lobj1);
            GObj_SetupGXLink(gobj, HSD_GObj_LObjCallback, 0, 0x80);
        }

        {
            HSD_GObj* gobj = GObj_Create(0xE, 0xF, 0);
            HSD_Fog* fog = HSD_FogLoadDesc(DP(HSD_FogDesc, sss_data_table->fog));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_FogKind, fog);
            GObj_SetupGXLink(gobj, fn_8025A974, 0, 0x80);
        }

        {
            HSD_JObj* jobj2;
            HSD_GObj* gobj;
            gobj = GObj_Create(4, 5, 0x80);
            attach_menu_model(gobj);
            {
                HSD_GObj* g = gobj;
                jobj2 = GET_JOBJ(g);
                HSD_GObj_SetupProc(g, fn_8025A91C, 0);
                HSD_JObjReqAnimAll(jobj2, 0.0F);
                HSD_JObjAnimAll(jobj2);
            }
        }

        {
            HSD_JObj* temp_r22_4;
            make_bg_model(&temp_r22_4);
            HSD_JObjReqAnimAll(temp_r22_4, 0.0F);
            HSD_JObjAnimAll(temp_r22_4);
        }

        make_icon_root(spDC);

        for (i = 0; i < 0x12; i++) {
            spDC[i + 1] = spDC[i]->next;
            HSD_JObjReqAnimAll(spDC[i + 1], 0.0F);
            HSD_JObjAnimAll(spDC[i + 1]);
        }

        for (i = 0; i < 0x1D; i++) {
            mnStageSel_803F06D0[i].x8 =
                gm_80164430(mnStageSel_803F06D0[i].stkind) ? 2 : 1;
        }

        for (i = 0; i <= 0xA; i++) {
            HSD_JObj* temp_r22_6;
            HSD_JObj* jobj;
            make_stage_icon(&temp_r22_6);
            jobj = temp_r22_6;
            lb_8000C1C0(jobj, spDC[i]);
            mnStageSel_803F06D0[i * 2].x0 = temp_r22_6->child->next;
            switch (mnStageSel_803F06D0[i * 2].x8) {
            case 0:
                HSD_JObjSetFlags(mnStageSel_803F06D0[i * 2].x0, JOBJ_HIDDEN);
                break;
            case 1:
                HSD_JObjReqAnimAllByFlags(mnStageSel_803F06D0[i * 2].x0, 0x10,
                                          1.0F);
                break;
            default:
                HSD_JObjReqAnimAllByFlags(mnStageSel_803F06D0[i * 2].x0, 0x10,
                                          mnStageSel_803F06D0[i * 2].x9 / 2 +
                                              2);
                break;
            }
            mnStageSel_803F06D0[i * 2 + 1].x0 = temp_r22_6->child;
            switch (mnStageSel_803F06D0[i * 2 + 1].x8) {
            case 0:
                HSD_JObjSetFlags(mnStageSel_803F06D0[i * 2 + 1].x0,
                                 JOBJ_HIDDEN);
                break;
            case 1:
                HSD_JObjReqAnimAllByFlags(mnStageSel_803F06D0[i * 2 + 1].x0,
                                          0x10, 1.0F);
                break;
            default:
                HSD_JObjReqAnimAllByFlags(
                    mnStageSel_803F06D0[i * 2 + 1].x0, 0x10,
                    mnStageSel_803F06D0[i * 2 + 1].x9 / 2 + 2);
                break;
            }
            HSD_JObjAnimAll(jobj);
            HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim,
                            AOBJ_ARG_AOV, NULL);
        }

        for (i = 0xB; i <= 0xF; i++) {
            HSD_JObj* jobj;
            HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
            s32 temp_r22_7;
            HSD_JObj* temp_r23_3;
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->icon_special.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x83);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 3);
            HSD_JObjAddAnimAll(jobj,
                               DP(HSD_AnimJoint,
                                  sss_models->icon_special.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->icon_special.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->icon_special.shapeanim_joint));
            temp_r23_3 = gobj->hsd_obj;
            lb_8000C1C0(temp_r23_3, spDC[i]);
            mnStageSel_803F06D0[i + 13].x0 = temp_r23_3;
            switch (mnStageSel_803F06D0[i + 13].x8) {
            case 1:
                mnStageSel_803F06D0[i + 13].x8 = 0;
                /* fallthrough */
            case 0:
                HSD_JObjSetFlagsAll(temp_r23_3, JOBJ_HIDDEN);
                break;
            default:
                temp_r22_7 = mnStageSel_803F06D0[i + 13].x9 - 0x16;
                do_anim(temp_r23_3, temp_r22_7);
                break;
            }
        }

        {
            HSD_JObj* jobj;
            HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
            HSD_JObj* temp_r22_8;
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->icon_random.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x83);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 3);
            HSD_JObjAddAnimAll(jobj,
                               DP(HSD_AnimJoint,
                                  sss_models->icon_random.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->icon_random.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->icon_random.shapeanim_joint));
            temp_r22_8 = gobj->hsd_obj;
            lb_8000C1C0(temp_r22_8, spDC[0x10]);
            do_anim(temp_r22_8, 2);
            mnStageSel_803F06D0[0x1D].x0 = temp_r22_8;
        }

        for (i = 0x11; i <= 0x12; i++) {
            HSD_JObj* jobj;
            HSD_AnimJoint* animjoint;
            HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
            s32 temp_r22_9;
            HSD_JObj* temp_r23_6;
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->icon_large.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x83);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 3);
            animjoint = DP(HSD_AnimJoint, sss_models->icon_large.animjoint);
            HSD_JObjAddAnimAll(jobj, animjoint,
                               DP(HSD_MatAnimJoint,
                                  sss_models->icon_large.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->icon_large.shapeanim_joint));

            temp_r23_6 = gobj->hsd_obj;
            lb_8000C1C0(temp_r23_6, spDC[i]);
            mnStageSel_803F06D0[i + 5].x0 = temp_r23_6;
            switch (mnStageSel_803F06D0[i + 5].x8) {
            case 1:
                mnStageSel_803F06D0[i + 5].x8 = 0;
                /* fallthrough */
            case 0:
                HSD_JObjSetFlagsAll(temp_r23_6, JOBJ_HIDDEN);
                break;
            default:
                temp_r22_9 = mnStageSel_803F06D0[i + 5].x9 - 0x14;
                do_anim(temp_r23_6, temp_r22_9);
                break;
            }
        }

        {
            HSD_JObj* jobj;
            HSD_GObj* gobj;
            gobj = GObj_Create(4, 5, 0x80);
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->cursor.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x86);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 2);
            HSD_JObjAddAnimAll(jobj,
                               DP(HSD_AnimJoint,
                                  sss_models->cursor.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->cursor.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->cursor.shapeanim_joint));

            {
                HSD_JObj* jobj2;
                HSD_GObj* g;
                jobj = get_jobj(gobj);
                spCC = mnStageSel_803B8550;
                g = gobj;
                jobj2 = jobj;
                HSD_GObj_SetupProc(g, fn_8025A310, 2);
                do_anim(jobj2, mnStageSel_804D50A0 + 1);
                HSD_JObjSetTranslate(jobj2, &spCC);
            }
        }

        {
            HSD_JObj* jobj;
            HSD_GObj* gobj;
            gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 5, 0x80);
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->stage_name.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x84);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 1);
            HSD_JObjAddAnimAll(jobj,
                               DP(HSD_AnimJoint,
                                  sss_models->stage_name.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->stage_name.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->stage_name.shapeanim_joint));
            {
                struct StageSelUserData* userdata;
                HSD_JObj* jobj = GET_JOBJ(gobj);
                userdata = HSD_MemAlloc(sizeof(struct StageSelUserData));
                GObj_InitUserData(gobj, 4, HSD_Free, userdata);
                HSD_GObj_SetupProc(gobj, fn_80259D84, 1);
                userdata->x0 = 0x1E;
                userdata->x4 = 0;
                userdata->x2 = 0;
            }
        }

        {
            HSD_JObj* jobj;
            HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
            jobj = HSD_JObjLoadJoint(DP(HSD_Joint,
                                        sss_models->stage_preview.joint));
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x81);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 1);
            HSD_JObjAddAnimAll(jobj,
                               DP(HSD_AnimJoint,
                                  sss_models->stage_preview.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->stage_preview.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->stage_preview.shapeanim_joint));

            {
                HSD_GObj* g;
                HSD_JObj* jobj;
                struct StagePreviewUserData* temp_r3_14;
                g = gobj;
                jobj = GET_JOBJ(g);
                temp_r3_14 = HSD_MemAlloc(sizeof(*temp_r3_14));
                GObj_InitUserData(g, 4, HSD_Free, temp_r3_14);
                HSD_GObj_SetupProc(g, fn_8025A090, 1);
                HSD_JObjReqAnimAll(jobj, 0.0F);
                HSD_JObjAnimAll(jobj);
                HSD_ForeachAnim(jobj, JOBJ_TYPE, ALL_TYPE_MASK,
                                HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL);
                HSD_JObjSetTranslateX(jobj, 100.0F);
                temp_r3_14->x0 = 0x1E;
                temp_r3_14->x4 = 0;
            }
        }

        {
            HSD_JObj* jobj;
            HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
            HSD_JObj* temp_r3_15 =
                HSD_JObjLoadJoint(DP(HSD_Joint,
                                     sss_models->icon_hover.joint));
            s32* temp_r3_16;
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, temp_r3_15);
            GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x85);
            HSD_GObj_SetupProc(gobj, mn_8022EAE0, 1);
            HSD_JObjAddAnimAll(temp_r3_15,
                               DP(HSD_AnimJoint,
                                  sss_models->icon_hover.animjoint),
                               DP(HSD_MatAnimJoint,
                                  sss_models->icon_hover.matanim_joint),
                               DP(HSD_ShapeAnimJoint,
                                  sss_models->icon_hover.shapeanim_joint));
            jobj = gobj->hsd_obj;
            temp_r3_16 = HSD_MemAlloc(sizeof(*temp_r3_16));
            GObj_InitUserData(gobj, 4, HSD_Free, temp_r3_16);
            *temp_r3_16 = 0;
            HSD_GObj_SetupProc(gobj, fn_8025A560, 1);
            HSD_JObjSetTranslateX(jobj, 100.0F);
        }

        lbAudioAx_80023F28(gmMainLib_8015ECB0());
#ifdef TARGET_PC
        if (pc_rank_session_active()) rankStageCreate();
        else if (netStageSel_Active()) netStageSel_TextCreate();
        frozen_create();
        ace_create();
#endif
    }
}

static inline HSD_PadStatus* get_pad(u8 i)
{
    return &HSD_PadCopyStatus[i];
}

/// OnFrame
void mnStageSel_Scene_OnFrame(void)
{
#ifdef TARGET_PC
    if (rank_text) {
        if (!pc_rank_session_active()) { gm_801A4B60(); return; }
        rankStageFrame();
        return;
    }
#endif
    if (sss_data->force_stage_id >= 0) {
        mnStageSel_804D6CAF = 2;
        sss_data->vs.start.rules.stkind = sss_data->force_stage_id;
        gm_801A4B60();
        return;
    }
    if (sss_data->no_lras == 0 && mn_8022F218()) {
        sfxBack();
        lb_800145F4();
        HSD_GObjFree(mnStageSel_804D6C9C);
        mn_8022F268();
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        gm_801A4B60();
        return;
    }
    bool b_pressed = false;
    if (mnStageSel_804D50A0 < 0) {
        mnStageSel_804D6CA0 = 0;
        mnStageSel_804D6CA0 |= HSD_PadCopyStatus[0].trigger;
        mnStageSel_804D6CA0 |= HSD_PadCopyStatus[1].trigger;
        mnStageSel_804D6CA0 |= HSD_PadCopyStatus[2].trigger;
        mnStageSel_804D6CA0 |= HSD_PadCopyStatus[3].trigger;
        b_pressed = (mnStageSel_804D6CA0 & 0x200) != 0;
        {
            int i;
            for (i = 0; i < 4; i++) {
                mnStageSel_804D6CAC = get_pad(i)->stickX;
                mnStageSel_804D6CAD = get_pad(i)->stickY;
                if (get_pad(i)->stickX < -0x1E || get_pad(i)->stickX > +0x1E ||
                    get_pad(i)->stickY < -0x1E || get_pad(i)->stickY > +0x1E)
                {
                    break;
                }
            }
        }
    } else {
        mnStageSel_804D6CA0 = get_pad(mnStageSel_804D50A0)->trigger;
        mnStageSel_804D6CAC = get_pad(mnStageSel_804D50A0)->stickX;
        mnStageSel_804D6CAD = get_pad(mnStageSel_804D50A0)->stickY;
#ifdef TARGET_PC
        if (netStageSel_Active() && !pc_rank_session_active()) {
            b_pressed = (HSD_PadCopyStatus[0].trigger & 0x200) ||
                        (HSD_PadCopyStatus[1].trigger & 0x200);
        } else
#endif
        {
            b_pressed = (mnStageSel_804D6CA0 & 0x200) != 0;
        }
    }
    if (mnStageSel_804D6CAC < -0x1E) {
        mnStageSel_804D6CAC += 0x1E;
    } else if (mnStageSel_804D6CAC > 0x1E) {
        mnStageSel_804D6CAC -= 0x1E;
    } else {
        mnStageSel_804D6CAC = 0;
    }
    if (mnStageSel_804D6CAD < -0x1E) {
        mnStageSel_804D6CAD += 0x1E;
    } else if (mnStageSel_804D6CAD > 0x1E) {
        mnStageSel_804D6CAD -= 0x1E;
    } else {
        mnStageSel_804D6CAD = 0;
    }
    if (mnStageSel_804D6CA4 != 0) {
        mnStageSel_804D6CA4 -= 1;
        return;
    }
    if (sss_data->x1 == 0 && b_pressed && mnStageSel_804D6CAF == 0)
    {
        sfxBack();
        gm_801A4B60();
    }
    if (mnStageSel_804D6CAF == 2) {
#ifdef TARGET_PC
        if (netStageSel_Active() && !pc_rank_session_active()) {
            if (pc_net_peer_status() != 0) {
                gm_801A4B60();
                return;
            }
            if (mnStageSel_804D6CAE >= NUM_STAGES) {
                mnStageSel_804D6CAE = netStageSel_Random();
            }
        }
#endif
        /* Never index the sentinel cells, including in offline play. */
        if (mnStageSel_804D6CAE < 0 || mnStageSel_804D6CAE >= NUM_STAGES) {
            mnStageSel_804D6CAE = mnStageSel_802599EC();
        }
        sss_data->vs.start.rules.stkind =
            mnStageSel_803F06D0[mnStageSel_804D6CAE].stkind;
#ifdef TARGET_PC
        if (grAce_Pick != GrAce_None &&
            mnStageSel_803F06D0[mnStageSel_804D6CAE].stkind == St_Kind_PStadium)
        {
            sss_data->vs.start.rules.stkind = GRACE_STKIND;
        } else {
            grAce_Pick = GrAce_None;
        }
        if (netStageSel_Active())
            pc_log_line("sss: resolved cell %d stage %d", mnStageSel_804D6CAE,
                        sss_data->vs.start.rules.stkind);
#endif
        gm_801A4B60();
    }
}

void mnStageSel_Scene_OnExit(UNUSED void* exit_data)
{
#ifdef TARGET_PC
    if (rank_text) { HSD_SisLib_803A5CC4(rank_text); rank_text = NULL; }
    if (net_text) { HSD_SisLib_803A5CC4(net_text); net_text = NULL; }
    if (frozen_text) { HSD_SisLib_803A5CC4(frozen_text); frozen_text = NULL; }
    frozen_root = frozen_tile = NULL;
    {
        int k;
        for (k = 0; k < GrAce_Count; k++) {
            if (ace_text[k]) { HSD_SisLib_803A5CC4(ace_text[k]); }
            ace_text[k] = NULL;
            ace_root[k] = ace_tile[k] = NULL;
        }
        ace_hover = GrAce_None;
    }
#endif
    if (mnStageSel_804D6C94 != NULL) {
        lbArchive_80016EFC(mnStageSel_804D6C94);
        mnStageSel_804D6C94 = NULL;
    }
    {
        SSSData* sss = sss_data;
        sss->start_game = mnStageSel_804D6CAF == 2 ? true : false;
        if (sss->start_game) {
            PreloadedGameModeState* cache = lbDvd_GetPreloadCacheScene();
            cache->game_cache.stkind = sss->vs.start.rules.stkind;
            lbDvd_80018254();
        }
    }
}

int mnSelStageRandom(void)
{
    return mnStageSel_803F06D0[mnStageSel_802599EC()].stkind;
}

int mnStageSel_8025BC08(int idx)
{
    return mnStageSel_803F06D0[idx].stkind;
}
