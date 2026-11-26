/* Per-match statistics for the replay viewer (PC only), in the spirit of
 * Slippi's: kills, deaths and their percents, damage, openings, neutral
 * wins, the best punish, L-cancels and actions per minute.
 *
 * Gathered each VS frame from the fighters themselves (gmStats_OnVsFrame).
 * Its statics are part of the rollback snapshot, like the rest of the
 * game's, so a re-simulated frame is counted once and both netplay peers
 * end with the same numbers. At the end of the match the game's own result
 * (winners, KOs, self-destructs) is added and the replay keeps the lot
 * (src/pc/replay.c). */
#ifndef MELEE_GM_GMSTATS_H
#define MELEE_GM_GMSTATS_H

#include <Runtime/platform.h>

struct MatchEnd;

#define GMSTATS_PORTS 4
#define GMSTATS_DEATHS 20
#define GMSTATS_VERSION 1

typedef struct GmMatchStatsDeath {
    u32 frame;   ///< match frame the stock was lost on
    f32 percent; ///< damage when it was lost
    u8 ckind;    ///< ::CharacterKind of the fighter that lost it
    s8 by;       ///< port that last hit it, or -1 (self-destruct)
    u8 pad[2];
} GmMatchStatsDeath;

typedef struct GmMatchStatsPlayer {
    u8 present;
    u8 kos;   ///< the game's count
    u8 falls; ///< stocks lost
    u8 sds;   ///< the game's count
    f32 damage_dealt;
    f32 damage_taken;
    u16 openings;      ///< punishes started on an opponent
    u16 kill_openings; ///< stocks taken, by last hit
    u16 neutral_wins;  ///< ... started while nobody was punishing this port
    u16 lcancel_ok;
    u16 lcancel_total;
    u16 ledge_grabs;
    f32 opening_damage; ///< damage over all punishes
    f32 best_punish;    ///< the most damage in one punish
    u32 actions;        ///< button presses and stick flicks (APM)
    u8 n_deaths;
    u8 pad[3];
    GmMatchStatsDeath deaths[GMSTATS_DEATHS];
} GmMatchStatsPlayer;

typedef struct GmMatchStats {
    u32 version; ///< GMSTATS_VERSION
    u32 frames;  ///< match length
    u8 has_result;
    u8 outcome;      ///< ::MatchOutcome
    u8 winners_mask; ///< bit per winning port
    u8 is_teams;
    GmMatchStatsPlayer p[GMSTATS_PORTS];
} GmMatchStats;

/// Start counting (each VS match's first frame).
void gmStats_Reset(void);
/// One VS frame.
void gmStats_OnVsFrame(void);
/// The game's own result, once it has worked it out (match end).
void gmStats_Finish(const struct MatchEnd* end);
/// The match's statistics so far.
const GmMatchStats* gmStats_Get(void);

#endif
