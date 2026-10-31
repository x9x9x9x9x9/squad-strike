#ifndef MELEE_FT_FTSDREMIX_H
#define MELEE_FT_FTSDREMIX_H

/* PC only: SD Remix's character changes (sdremix.games), on when the
 * launcher's "SD Remix characters" setting is (src/pc/sdremix.cpp, which
 * also swaps in SD Remix's fighter files). Each FT_SDR check in src/melee
 * is one of its DOL mods (MCM_Mods/2_Balance.txt in its repository). */

#include <stdbool.h>
#include <melee/ft/forward.h>

#ifdef TARGET_PC
#include "pc/pc.h"
#define FT_SDR pc_sdremix_active()
/// SD Remix's FSM engine: animation speed changes. End of ftAction_80073240.
void ftSdr_ApplyFsm(Fighter_GObj* gobj);
/// SD Remix's motion-table changes; once, at boot (pc_sdremix_init).
void ftSdr_PatchTables(void);
#else
#define FT_SDR false
#endif

#endif
