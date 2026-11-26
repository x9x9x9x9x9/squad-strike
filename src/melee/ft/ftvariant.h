/* Per-player fighter versions picked on character select (PC only).
 *
 * The 20XX HackPack's SD Remix Captain Falcon (its PlCa.sat): grounded
 * down-B becomes a quick shine-like kick on the down-throw animation, and
 * its ending motion is gone. Those are three entries of Falcon's motion
 * table (fighter data only, no code), so a player who picks it gets a copy
 * of the table with them changed; everyone else keeps the disc's.
 * D-pad left on character select switches it (mncharsel_variant.h). */
#ifndef MELEE_FT_FTVARIANT_H
#define MELEE_FT_FTVARIANT_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#define FTVARIANT_FALCON_20XX (1 << 0)

/// Each port's versions (FTVARIANT_* bits). Character select sets them;
/// replays restore them.
extern u8 ftVariant_ports[4];

/// Point this fighter at its version's motion table, when it has one.
/// Called once its kind's data is loaded (ftData_80085B10).
void ftVariant_Apply(Fighter* fp);

/// Back to the disc's fighters for everyone (a new netplay session).
void ftVariant_Reset(void);

#endif
