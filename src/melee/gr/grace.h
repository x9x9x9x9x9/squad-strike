#ifndef MELEE_GR_GRACE_H
#define MELEE_GR_GRACE_H

/* PC only: three stages from the ACE build (github.com/Chri222k/ACE-BUILD-
 * PUBLIC-), an m-ex build of Melee: Metal Cavern (Smash 64), WarioWare and
 * Realgam Colosseum.
 *
 * Their files ship in resources/ace (an "ace" folder in MELEE_ACE_DIR, the
 * user data folder or next to melee.exe takes precedence); src/pc/
 * sdremix.cpp adds them to the disc at boot, so the ACE disc itself is never
 * needed:
 *   GrOMc.dat, GrWw.dat, GrColosseum.dat     the stages (ACE's files/)
 *   mcavern.hps, wahwareinc.hps, normalbattle.hps, cipherpeon.hps,
 *   battlemode1.hps                          their music (ACE's files/audio/)
 *   icon_064.tex, icon_146.tex, icon_164.tex the tiles' pictures (the ACE
 *                                            repository's assets/sss/)
 * A stage whose .dat is missing has no tile; a missing track plays a disc
 * one instead (grAce_Bgm); a missing icon leaves Stadium's picture.
 *
 * MELEE_DEBUG_VS_ACE=1|2|3 starts the debug VS match on one of them.
 *
 * In m-ex each stage is a new stage id built on the Sheik target stage: it
 * reuses that stage's callbacks (and, for Colosseum, Battlefield's collision
 * materials), and WarioWare and Metal Cavern carry a little PowerPC code of
 * their own in the file (its grFunction symbol). Here they play on the Sheik
 * target stage's ids (GRACE_STKIND, Gr_Kind_TSeak): while one is picked,
 * stage_datas[Gr_Kind_TSeak] is that stage's StageData (Ground_SyncAceStage
 * in ground.c), whose callbacks are the file's code ported to C (grace.c),
 * so that code never runs. Netplay: the host's files decide which tiles
 * show, so both players need the same ace folder. The stage select
 * screen picks one with its own tiles next to the FROZEN Stadium tile
 * (mnstagesel.c), behind the Stage Expansion setting. */

#include <stdbool.h>
#include <melee/gr/forward.h>

/// The Sheik target stage's StKind (Gr_Kind_TSeak's): the ACE stages' too.
#define GRACE_STKIND ((StKind) 0x35)

typedef enum GrAceStage {
    GrAce_None,
    GrAce_MetalCavern,
    GrAce_WarioWare,
    GrAce_Colosseum,
    GrAce_Count,
} GrAceStage;

/// An ACE stage's Random Stage switch: its bit in GamePrefs.stage_mask
/// (the disc's 29 stages use bits 0-28). On by default; the Random Stage
/// menu (mnstagesw.c) lists them after Dream Land N64's column.
#define GRACE_SWITCH_BIT(stage) (28 + (stage))

#ifdef TARGET_PC
/// The ACE stage the stage select screen picked for the next match, or
/// GrAce_None. Set on the stage select screen (and MELEE_DEBUG_VS_ACE).
extern int grAce_Pick;

/// The ACE stage in play: grAce_Pick in a mode that picks its stage on the
/// stage select screen, else GrAce_None. Target Test, Classic and the like
/// keep the Sheik target stage.
GrAceStage grAce_Current(void);
/// stage_datas[Gr_Kind_TSeak] for grAce_Current(), or NULL for the target
/// stage's own.
StageData* grAce_StageData(void);
/// Its display name ("WarioWare").
const char* grAce_Name(int stage);
/// The tile icon's file in the ace folder ("icon_146.tex").
const char* grAce_IconFile(int stage);
/// The stage's music for this match: a BGM id lbAudioAx_80023F28 plays
/// (lbAudioAx_AceBgm ids, past the disc's 0x62).
int grAce_Bgm(void);
/// Whether this stage can be offered: Stage Expansion on, and its file in the
/// ace folder (src/pc/sdremix.cpp). In netplay the host decides (RULES).
bool grAce_Offered(int stage);
#else
#define grAce_Current() GrAce_None
#endif

#endif
