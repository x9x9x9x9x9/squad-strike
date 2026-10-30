#ifndef MELEE_GM_GMSQUAD_H
#define MELEE_GM_GMSQUAD_H

/* PC only: Squad Strike (Smash Ultimate's tag team) for 1v1 VS matches.
 *
 * Each player builds a squad of up to four fighters on the character select
 * screen: the coin pick, then up to three more added with Z. Every fighter
 * gets one stock; when one is KO'd, the next drops in on the revival
 * platform. Ported from the "Squad Strike v4" Gecko codes for Dolphin.
 *
 * On PC the preload heap and ARAM are enlarged (lbheap.c, lbmemory.c, pc.h),
 * so every squad member's files are preloaded on character select and
 * initialised at match setup (gmSquad_WarmFiles): nothing streams from disc
 * during a match. That keeps the swap a plain game-state change, which the
 * netplay snapshots roll back like any other. Squads are offered offline and
 * in LAN / Direct netplay; never in Unranked or Ranked.
 *
 * All state here is ordinary game statics (melee_bss), so it is snapshotted,
 * and every decision is a function of synced inputs. */

#include <stdbool.h>
#include <dolphin/types.h>
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/lb/forward.h>
#include <melee/mn/forward.h>

#ifdef TARGET_PC

struct GameCache;

#define GMSQUAD_SIZE 4          /* fighters per squad, the coin pick included */
#define GMSQUAD_EXTRA (GMSQUAD_SIZE - 1)
#define GMSQUAD_STOCKS_PER_FIGHTER 1
#define GMSQUAD_STOCKS (GMSQUAD_SIZE * GMSQUAD_STOCKS_PER_FIGHTER)
#define GMSQUAD_PORTS 4
#define GMSQUAD_NONE 0xFF

typedef struct GmSquadMember {
    u8 ckind; ///< ::CharacterKind, or GMSQUAD_NONE
    u8 color;
} GmSquadMember;

/// Squad members after the coin pick, per CSS port, packed from index 0.
extern GmSquadMember gmSquad_rows[GMSQUAD_PORTS][GMSQUAD_EXTRA];

/// Squads may exist in the current game mode (VS, or LAN/Direct online).
bool gmSquad_ModeAllowed(void);
int gmSquad_Count(int port);
bool gmSquad_Any(void);
void gmSquad_Clear(int port);
void gmSquad_ClearAll(void);
/// Changes whenever any squad changes; the preloader compares it.
u32 gmSquad_Key(void);
/// Clears squads left over from another netplay session (or offline).
void gmSquad_CheckSession(void);
/// Sound banks of every squad member (lbAudioAx_80026E84 masks).
u64 gmSquad_SoundMask(void);

/// lbDvd_80017960: request squad members' files (and Kirby's copies).
void gmSquad_AddPreloads(struct GameCache* cache);
/// gmVsMelee_EnterVs: decide whether this match is a squad match and give
/// everyone GMSQUAD_STOCKS stocks if it is.
void gmSquad_OnEnterVs(StartMeleeData* start);
/// Any other match (sudden death, other modes): squads off.
void gmSquad_Disable(void);
/// fn_8016E124 (end of VS setup, and after each swap): initialise every
/// squad member's preloaded files while the match is still in its lockstep
/// setup, so no archive is first parsed mid-match.
void gmSquad_WarmFiles(void);
/// fn_8016719C: true to hold this respawn because a squad member is due.
bool gmSquad_HoldRespawn(s32 slot, s32 subchar);
/// fn_8016A4C8 (every VS frame): swap held players and respawn them.
void gmSquad_OnVsFrame(void);

#endif

#endif
