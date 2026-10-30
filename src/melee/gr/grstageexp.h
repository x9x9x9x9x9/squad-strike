#ifndef MELEE_GR_GRSTAGEEXP_H
#define MELEE_GR_GRSTAGEEXP_H

/* PC only: UnclePunch's "Stage Expansion" Gecko code, ported to C.
 *
 * It turns Castle, Onett, Mute City, Rainbow Cruise, Jungle Japes, Great
 * Bay, Yoshi's Island, Green Greens, Fourside, Mushroom Kingdom I and II and
 * Flat Zone into hazard-free tournament layouts: per-stage patches to the
 * stage file once it is loaded (grstageexp.c), plus hazards switched off in
 * each stage's code (the grStageExp_On() checks in gr*.c).
 *
 * On by default: the launcher's Gameplay settings (and the in-game menu)
 * switch it, and MELEE_STAGE_EXPANSION=0|1 overrides that. In netplay the
 * host's setting applies to both peers (RULES).
 *
 * Rainbow Cruise is left out by default: its layout crashes (it does with the
 * Gecko code on Dolphin too). MELEE_STAGE_EXPANSION_RCRUISE=1 tries it. */

#include <stdbool.h>
#include <melee/gr/forward.h>

#ifdef TARGET_PC
bool grStageExp_On(void);
/// Take the current setting for the stage about to load (gmvs.c).
void grStageExp_Latch(void);
/// grStageExp_On() for Rainbow Cruise: also needs
/// MELEE_STAGE_EXPANSION_RCRUISE=1.
bool grStageExp_RCruiseOn(void);
/// Patch the loaded stage file for this stage (from fn_8016E730, right after
/// Stage_802251E8 loads it, as the Gecko code's hook at 8016E800 did).
void grStageExp_PatchStageFile(StKind stkind);
#else
#define grStageExp_On() false
#define grStageExp_RCruiseOn() false
#endif

#endif
