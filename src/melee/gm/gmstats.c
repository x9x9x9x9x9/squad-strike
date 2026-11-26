#include "gmstats.h"

#include "types.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* A punish ends when its victim goes this long without being hit again
 * (Slippi's 45 frames). */
#define GMSTATS_PUNISH_GAP 45
/* An L-cancel: L, R or Z within this many frames before an aerial lands. */
#define GMSTATS_LCANCEL_WINDOW 7

typedef struct GmMatchStatsPunish {
    s8 by; ///< attacking port, or -1 for none
    u8 pad[3];
    u32 last_hit; ///< frame of its latest hit
    f32 damage;
} GmMatchStatsPunish;

/* All of it is game state (it rolls back with the match). */
static GmMatchStats s_stats;
static f32 s_percent[GMSTATS_PORTS];
static s32 s_stocks[GMSTATS_PORTS];
static s32 s_motion[GMSTATS_PORTS];
static u32 s_trigger_at[GMSTATS_PORTS]; ///< frame of the last L/R/Z press, +1
static u8 s_stick_region[GMSTATS_PORTS];
static u8 s_cstick_region[GMSTATS_PORTS];
static GmMatchStatsPunish s_punish[GMSTATS_PORTS]; ///< on each port, as victim
static bool s_started;

void gmStats_Reset(void)
{
    int i;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.version = GMSTATS_VERSION;
    for (i = 0; i < GMSTATS_PORTS; i++) {
        s_percent[i] = 0.0f;
        s_stocks[i] = -1;
        s_motion[i] = -1;
        s_trigger_at[i] = 0;
        s_stick_region[i] = 0;
        s_cstick_region[i] = 0;
        s_punish[i].by = -1;
        s_punish[i].damage = 0.0f;
        s_punish[i].last_hit = 0;
    }
    s_started = true;
}

const GmMatchStats* gmStats_Get(void)
{
    return &s_stats;
}

/* A stick's direction as one of nine regions (centre and eight), so a
 * flick to a new one counts as an action and holding it does not. */
static u8 stick_region(f32 x, f32 y)
{
    const f32 dead = 0.2875f;
    int col = x > dead ? 2 : x < -dead ? 0 : 1;
    int row = y > dead ? 2 : y < -dead ? 0 : 1;
    return (u8) (row * 3 + col);
}

static int popcount(u32 v)
{
    int n = 0;
    while (v != 0) {
        v &= v - 1;
        n++;
    }
    return n;
}

static void end_punish(int victim, bool killed)
{
    GmMatchStatsPunish* pu = &s_punish[victim];
    if (pu->by >= 0 && pu->by < GMSTATS_PORTS) {
        GmMatchStatsPlayer* a = &s_stats.p[(int) pu->by];
        a->opening_damage += pu->damage;
        if (pu->damage > a->best_punish) {
            a->best_punish = pu->damage;
        }
        if (killed) {
            a->kill_openings++;
        }
    }
    pu->by = -1;
    pu->damage = 0.0f;
}

void gmStats_OnVsFrame(void)
{
    u32 frame;
    int i;

    if (!s_started) {
        gmStats_Reset();
    }
    frame = ++s_stats.frames;
    for (i = 0; i < GMSTATS_PORTS; i++) {
        HSD_GObj* gobj = Player_GetEntity(i);
        GmMatchStatsPlayer* me = &s_stats.p[i];
        Fighter* fp;
        s32 stocks;
        f32 percent;
        u32 pressed;
        u8 region;
        s32 motion;

        if (gobj == NULL || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        fp = GET_FIGHTER(gobj);
        me->present = 1;
        stocks = Player_GetStocks(i);
        percent = fp->dmg.x1830_percent;

        /* Actions: new button presses, and each stick moved to a new
         * region (Slippi counts the same). */
        pressed = fp->input.pressed_buttons &
                  (HSD_PAD_A | HSD_PAD_B | HSD_PAD_X | HSD_PAD_Y | HSD_PAD_Z |
                   HSD_PAD_L | HSD_PAD_R);
        me->actions += popcount(pressed);
        region = stick_region(fp->input.lstick[0].x, fp->input.lstick[0].y);
        if (region != s_stick_region[i]) {
            if (region != 4) {
                me->actions++;
            }
            s_stick_region[i] = region;
        }
        region = stick_region(fp->input.cstick[0].x, fp->input.cstick[0].y);
        if (region != s_cstick_region[i]) {
            if (region != 4) {
                me->actions++;
            }
            s_cstick_region[i] = region;
        }
        if (pressed & (HSD_PAD_L | HSD_PAD_R | HSD_PAD_Z)) {
            s_trigger_at[i] = frame + 1;
        }

        /* Motion changes: aerial landings and ledge grabs. */
        motion = (s32) fp->motion_id;
        if (motion != s_motion[i]) {
            if (motion >= ftCo_MS_LandingAirN && motion <= ftCo_MS_LandingAirLw) {
                me->lcancel_total++;
                if (s_trigger_at[i] != 0 &&
                    frame + 1 - s_trigger_at[i] <= GMSTATS_LCANCEL_WINDOW)
                {
                    me->lcancel_ok++;
                }
            } else if (motion == ftCo_MS_CliffCatch) {
                me->ledge_grabs++;
            }
            s_motion[i] = motion;
        }

        /* Damage, by whoever last hit this fighter. */
        if (s_stocks[i] >= 0 && percent > s_percent[i] + 0.01f) {
            f32 dealt = percent - s_percent[i];
            int by = fp->dmg.x18c4_source_ply;
            me->damage_taken += dealt;
            if (by >= 0 && by < GMSTATS_PORTS && by != i) {
                GmMatchStatsPunish* pu = &s_punish[i];
                s_stats.p[by].damage_dealt += dealt;
                if (pu->by != by) {
                    end_punish(i, false);
                    s_stats.p[by].openings++;
                    /* Neutral: the attacker was not being punished. */
                    if (s_punish[by].by < 0) {
                        s_stats.p[by].neutral_wins++;
                    }
                    pu->by = (s8) by;
                }
                pu->damage += dealt;
                pu->last_hit = frame;
            }
        }
        if (s_punish[i].by >= 0 && frame - s_punish[i].last_hit > GMSTATS_PUNISH_GAP) {
            end_punish(i, false);
        }

        /* A stock lost: to whoever hit this fighter last, as the game keeps
         * it for its own KO count (the punish has usually ended by the time
         * a fighter reaches the blast zone). */
        if (s_stocks[i] >= 0 && stocks < s_stocks[i]) {
            int by = fp->dmg.x18c4_source_ply;
            if (by < 0 || by >= GMSTATS_PORTS || by == i) {
                by = -1;
            }
            if (me->n_deaths < GMSTATS_DEATHS) {
                GmMatchStatsDeath* d = &me->deaths[me->n_deaths++];
                d->frame = frame;
                d->percent = s_percent[i];
                d->ckind = (u8) Player_GetPlayerCharacter(i);
                d->by = (s8) by;
            }
            me->falls++;
            if (by >= 0) {
                s_stats.p[by].kill_openings++;
            }
            end_punish(i, false);
        }
        s_stocks[i] = stocks;
        s_percent[i] = percent;
    }
}

void gmStats_Finish(const struct MatchEnd* end)
{
    int i, j;
    s_started = false;
    if (end == NULL) {
        return;
    }
    for (i = 0; i < GMSTATS_PORTS; i++) {
        if (s_punish[i].by >= 0) {
            end_punish(i, false);
        }
    }
    s_stats.has_result = 1;
    s_stats.outcome = end->outcome;
    s_stats.is_teams = end->is_teams;
    s_stats.winners_mask = 0;
    for (i = 0; i < end->n_winners && i < GM_MAX_PLAYERS; i++) {
        if (end->winners[i] < GMSTATS_PORTS) {
            s_stats.winners_mask |= (u8) (1 << end->winners[i]);
        }
    }
    for (i = 0; i < GMSTATS_PORTS; i++) {
        const struct MatchPlayerData* pd = &end->player_standings[i];
        u32 kos = 0;
        for (j = 0; j < GM_MAX_PLAYERS; j++) {
            if (j != i) {
                kos += pd->kills[j];
            }
        }
        s_stats.p[i].kos = (u8) (kos > 255 ? 255 : kos);
        s_stats.p[i].sds = (u8) (pd->self_destructs > 255 ? 255 : pd->self_destructs);
    }
}
