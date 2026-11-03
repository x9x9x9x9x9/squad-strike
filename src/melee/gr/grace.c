#include "grace.h"

#ifdef TARGET_PC

#include <stddef.h>

#include "granime.h"
#include "grdatfiles.h"
#include "grdisplay.h"
#include "grmaterial.h"
#include "ground.h"
#include "grtseak.h"
#include "inlines.h"
#include "types.h"
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/forward.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>

#include "pc/pc.h"

int grAce_Pick;

/* lbaudio_ax.c: the ACE tracks, after the disc's 0x62 BGM ids. */
enum {
    AceBgm_MetalCavern = 0x62,
    AceBgm_WarioWare,
    AceBgm_NormalBattle,
    AceBgm_CipherPeon,
    AceBgm_BattleMode1,
};

/* Every map id a file can have reads its row here (Ground_GetStageGObj and
 * the light and fog searches index it by map id), so each table is longer
 * than any of the three files' map_head. */
#define ACE_MAP_MAX 32

static void ace_noop(Ground_GObj* gobj) {}
static bool ace_false(Ground_GObj* gobj)
{
    return false;
}
static bool ace_true(Ground_GObj* gobj)
{
    return true;
}
static void ace_demo_init(s32 arg) {}
static void ace_event(void) {}
static bool ace_pred(void)
{
    return false;
}
static lbColl_80008D30_arg1* ace_touch_line(enum_t arg)
{
    return NULL;
}
static bool ace_shadow(Vec3* pos, int arg1, HSD_JObj* jobj)
{
    return true;
}
static void ace_wind(Ground_GObj* gobj)
{
    lb_800115F4();
}
static void ace_update_coll(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
}

/* ---- WarioWare (GrWw.dat's grFunction) ----------------------------------
 * Every map part is set up the same way: collision bound to the model,
 * animation set 0, collision following the model each frame. Its callbacks
 * table is the Sheik target stage's (lights and fog from part 2). */
static StageCallbacks ww_callbacks[ACE_MAP_MAX] = {
    { 0 }, { 0 }, { NULL, NULL, NULL, NULL, 0xC0000000 },
};

static void ww_part(int map_id)
{
    Ground_GObj* gobj = Ground_GetStageGObj(map_id);
    Ground* gp;

    if (gobj == NULL) {
        return;
    }
    gp = GET_GROUND(gobj);
    gp->x8_callback = NULL;
    gp->xC_callback = ace_noop;
    GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 3, 0);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
    HSD_GObj_SetupProc(gobj, ace_update_coll, 4);
}

static void ww_on_init(void)
{
    int n = grDatFiles_801C6330(0)->unk4->unkC;
    HSD_GObj* wind;
    int i;

    if (n > ACE_MAP_MAX) {
        n = ACE_MAP_MAX;
    }
    for (i = 0; i < n; i++) {
        ww_part(i);
    }
    wind = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
    if (wind != NULL) {
        HSD_GObj_SetupProc(wind, ace_wind, 4);
    }
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* ---- Metal Cavern (GrOMc.dat's grFunction) -------------------------------
 * Three parts: 0 animated scenery, 1 the background (no collision, drawn
 * with material flag 0x28000000 and its own render order), 2 the floor. */
static void omc_scenery_init(Ground_GObj* gobj)
{
    Ground_StartMapAnim(gobj);
}

static void omc_bg_init(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x10_flags.b2 = 0;
    grMaterial_801C8858(GET_JOBJ(gobj), 0x28000000);
    gp->x11_flags.b012 = 2;
}

static void omc_floor_init(Ground_GObj* gobj)
{
    Ground_InitMapCollAndAnim(gobj);
}

static void omc_floor_proc(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
    lb_800115F4();
}

static StageCallbacks omc_callbacks[ACE_MAP_MAX] = {
    { omc_scenery_init, ace_true, ace_noop, ace_noop, 0 },
    { omc_bg_init, ace_true, ace_update_coll, ace_noop, 0 },
    { omc_floor_init, ace_true, omc_floor_proc, ace_noop, 0xC0000000 },
};

static void omc_on_init(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        Ground_GObj* gobj = Ground_GetStageGObj(i);
        if (gobj != NULL) {
            Ground_SetupStageCallbacks(gobj, &omc_callbacks[i]);
        }
    }
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* ---- Realgam Colosseum (GrColosseum.dat: no code) -------------------------
 * The Sheik target stage's own callbacks, with Battlefield's table for the
 * light and fog searches (no part flagged: default lights, no fog) and
 * Battlefield's collision materials (mplib.c, mpLib_AceMaterials). */
static StageCallbacks colo_callbacks[ACE_MAP_MAX];

static StageData s_data[GrAce_Count];

static void build(void)
{
    static bool built;
    StageData* d;

    if (built) {
        return;
    }
    built = true;

    d = &s_data[GrAce_MetalCavern];
    *d = grTSk_StageData;
    d->callbacks = omc_callbacks;
    d->data1 = "/GrOMc.dat";
    d->on_init = omc_on_init;
    d->on_demo_init = ace_demo_init;
    d->on_load = ace_event;
    d->on_start = ace_event;
    d->callback4 = ace_pred;
    d->on_touch_line = ace_touch_line;
    d->on_check_shadow_render = ace_shadow;

    d = &s_data[GrAce_WarioWare];
    *d = s_data[GrAce_MetalCavern];
    d->callbacks = ww_callbacks;
    d->data1 = "/GrWw.dat";
    d->on_init = ww_on_init;

    d = &s_data[GrAce_Colosseum];
    *d = grTSk_StageData;
    d->callbacks = colo_callbacks;
    d->data1 = "/GrColosseum.dat";
}

GrAceStage grAce_Current(void)
{
    if (grAce_Pick <= GrAce_None || grAce_Pick >= GrAce_Count) {
        return GrAce_None;
    }
    switch (gm_GetCurrentGameMode()) {
    case GM_CLASSIC:
    case GM_ADVENTURE:
    case GM_ALLSTAR:
    case GM_TARGET_TEST:
    case GM_HOME_RUN_CONTEST:
    case GM_EVENT:
    case GM_10MAN_VS:
    case GM_100MAN_VS:
    case GM_3MIN_VS:
    case GM_15MIN_VS:
    case GM_ENDLESS_VS:
    case GM_CRUEL_VS:
        return GrAce_None; /* these load the target stage itself */
    default:
        return (GrAceStage) grAce_Pick;
    }
}

StageData* grAce_StageData(void)
{
    GrAceStage k = grAce_Current();
    if (k == GrAce_None) {
        return NULL;
    }
    build();
    return &s_data[k];
}

const char* grAce_Name(int stage)
{
    switch (stage) {
    case GrAce_MetalCavern:
        return "Metal Cavern";
    case GrAce_WarioWare:
        return "WarioWare";
    case GrAce_Colosseum:
        return "Colosseum";
    default:
        return "";
    }
}

const char* grAce_IconFile(int stage)
{
    switch (stage) {
    case GrAce_MetalCavern:
        return "icon_064.tex";
    case GrAce_WarioWare:
        return "icon_146.tex";
    case GrAce_Colosseum:
        return "icon_164.tex";
    default:
        return NULL;
    }
}

/* A track the ace folder holds, else a disc one in its spirit. */
static int ace_track(int id, const char* file, int fallback)
{
    return pc_ace_has_file(file) ? id : fallback;
}

int grAce_Bgm(void)
{
    switch (grAce_Current()) {
    case GrAce_MetalCavern:
        return ace_track(AceBgm_MetalCavern, "mcavern.hps", 0x50); /* sp_metal */
    case GrAce_WarioWare:
        return ace_track(AceBgm_WarioWare, "wahwareinc.hps", 0x9); /* famidemo */
    case GrAce_Colosseum: {
        /* ACE's playlist: the three Pokémon Colosseum battle themes, equally
         * likely. */
        static const char* const files[3] = {
            "normalbattle.hps", "cipherpeon.hps", "battlemode1.hps",
        };
        int i = HSD_Randi(3);
        return ace_track(AceBgm_NormalBattle + i, files[i], 0x3F); /* pokesta */
    }
    default:
        return -1;
    }
}

bool grAce_Offered(int stage)
{
    if (stage <= GrAce_None || stage >= GrAce_Count) {
        return false;
    }
    return pc_is_stage_expansion_enabled() && pc_ace_stage_offered(stage);
}

#endif
