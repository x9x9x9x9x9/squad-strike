/* Exercise the shipping random-stage selector, including sentinel inputs. */
#define __assert libc_assert
#include <assert.h>
#undef __assert
#include <stdio.h>
#include "../src/melee/mn/mnstagesel.c"
bool grStadium_FrozenPick;
static uint32_t seed;
static int enabled = -1;
uint32_t pc_net_seed(void) {
    return seed;
}
int pc_net_local_player(void) {
    return 0;
}
bool gm_80164330(s32 stage) {
    return enabled == -1 || stage == enabled;
}
/* The ACE stages: offered and switched on only when ace_on. */
static bool ace_on;
int grAce_Pick;
bool grAce_Offered(int stage) {
    return ace_on && stage > GrAce_None && stage < GrAce_Count;
}
bool gm_IsStageUnlocked(u16 bit) {
    return ace_on && bit >= GRACE_SWITCH_BIT(1);
}
const char* grAce_Name(int stage) {
    (void) stage;
    return "ace";
}
bool pc_rank_session_active(void) {
    return false;
}
void pc_log_line(const char* fmt, ...) {
    (void) fmt;
}
int main(void) {
    for (seed = 0; seed < 10000; ++seed) {
        for (int cell = 29; cell <= 30; ++cell) {
            mnStageSel_804D6CAE = cell;
            int pick = netStageSel_Random();
            assert(pick >= 0 && pick < NUM_STAGES);
            assert(mnStageSel_803F06D0[pick].stkind != 0);
            assert(netStageSel_Random() == pick);
        }
    }
    assert(grAce_Pick == GrAce_None);

    /* With the ACE switches on, Random lands on them too, as Pokemon
     * Stadium's cell plus grAce_Pick, the same way on both peers. */
    ace_on = true;
    int ace_hits[GrAce_Count] = { 0 };
    for (seed = 0; seed < 10000; ++seed) {
        mnStageSel_804D6CAE = 29;
        int pick = netStageSel_Random();
        int ace = grAce_Pick;
        assert(netStageSel_Random() == pick && grAce_Pick == ace);
        if (ace != GrAce_None) {
            assert(mnStageSel_803F06D0[pick].stkind == St_Kind_PStadium);
        }
        ace_hits[ace]++;
    }
    for (int k = GrAce_None + 1; k < GrAce_Count; k++) {
        assert(ace_hits[k] > 0);
    }
    /* Only ACE stages on: Random still lands on one of them. */
    enabled = 1000;
    netStageSel_Random();
    assert(grAce_Pick != GrAce_None);
    ace_on = false;

    enabled = mnStageSel_803F06D0[7].xA;
    assert(netStageSel_Random() == 7);
    enabled = 1000;
    assert(netStageSel_Random() == 0);
    puts("PASS: synchronized random selection excludes sentinel cells and obeys stage switches");
}
