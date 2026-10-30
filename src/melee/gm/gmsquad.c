#include "gmsquad.h"

#ifdef TARGET_PC

#include <string.h>

#include "gm_1601.h"
#include "gm_1A3F.h"
#include "gmonlinemode.h"
#include "gmvs.h"
#include "types.h"
#include <melee/ef/efasync.h>
#include <melee/ft/ft_0D4D.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftlib.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include "pc/net.h"
#include "pc/pc.h"

/* Squad members after the coin pick, per port (character, colour). */
GmSquadMember gmSquad_rows[GMSQUAD_PORTS][GMSQUAD_EXTRA];

/* Whether the table above has been initialised, and for which netplay
 * session (pc_net_seed()) or 0 for offline play. */
static bool s_init;
static u32 s_session;

/* The match in progress is a squad match. */
static bool s_live;
/* Respawns held by gmSquad_HoldRespawn, waiting for gmSquad_OnVsFrame. */
static bool s_held[GMSQUAD_PORTS];
/* Set while gmSquad_OnVsFrame calls fn_8016719C itself. */
static bool s_respawning;
/* Stocks everyone started this squad match with: the biggest squad's size,
 * one per fighter (a smaller squad's last fighter keeps the spare ones). */
static s8 s_stocks;
/* Each player's lineup for the stock icons: coin pick, then members. */
static GmSquadMember s_lineup[GMSQUAD_PORTS][GMSQUAD_SIZE];
static u8 s_lineup_count[GMSQUAD_PORTS];

static void init(void)
{
    if (!s_init) {
        memset(gmSquad_rows, GMSQUAD_NONE, sizeof(gmSquad_rows));
        s_init = true;
    }
}

bool gmSquad_ModeAllowed(void)
{
    if (!pc_is_squad_strike_enabled()) {
        return false; /* launcher / in-game menu: Squad Strike off */
    }
    switch (gm_GetCurrentGameMode()) {
    case GM_VS:
        return true;
    case GM_ONLINE:
        /* Matchmaking pairs strangers on the vanilla ruleset: no squads. */
        return gmOnline_GetKind() == ONLINE_KIND_LAN ||
               gmOnline_GetKind() == ONLINE_KIND_DIRECT;
    default:
        return false;
    }
}

int gmSquad_Count(int port)
{
    int n = 0;
    int i;
    init();
    if (port < 0 || port >= GMSQUAD_PORTS) {
        return 0;
    }
    for (i = 0; i < GMSQUAD_EXTRA; i++) {
        if (gmSquad_rows[port][i].ckind != GMSQUAD_NONE) {
            n++;
        }
    }
    return n;
}

int gmSquad_StocksFor(void)
{
    int need = 0;
    int p;
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        if (gmSquad_Count(p) + 1 > need) {
            need = gmSquad_Count(p) + 1;
        }
    }
    return need * GMSQUAD_STOCKS_PER_FIGHTER;
}

bool gmSquad_Any(void)
{
    int p;
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        if (gmSquad_Count(p) != 0) {
            return true;
        }
    }
    return false;
}

void gmSquad_Clear(int port)
{
    init();
    if (port >= 0 && port < GMSQUAD_PORTS) {
        memset(gmSquad_rows[port], GMSQUAD_NONE, sizeof(gmSquad_rows[port]));
    }
}

void gmSquad_ClearAll(void)
{
    init();
    memset(gmSquad_rows, GMSQUAD_NONE, sizeof(gmSquad_rows));
}

u32 gmSquad_Key(void)
{
    u32 key = 2166136261u;
    const u8* p = (const u8*) gmSquad_rows;
    size_t i;
    init();
    for (i = 0; i < sizeof(gmSquad_rows); i++) {
        key = (key ^ p[i]) * 16777619u;
    }
    return key;
}

void gmSquad_CheckSession(void)
{
    u32 session = pc_net_active() ? (pc_net_seed() | 1u) : 0;
    init();
    if (session != s_session) {
        /* Both peers start a session with the same (empty) squads. */
        gmSquad_ClearAll();
        s_session = session;
    }
}

u64 gmSquad_SoundMask(void)
{
    u64 mask = 0;
    int p, i;
    init();
    if (!gmSquad_ModeAllowed()) {
        return 0;
    }
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            u8 ck = gmSquad_rows[p][i].ckind;
            if (ck < ChKind_Max) {
                mask |= lbAudioAx_80026E84(ck);
            }
        }
    }
    return mask;
}

/* ---- preloading ---------------------------------------------------------- */

void gmSquad_AddPreloads(struct GameCache* cache)
{
    CharacterKind lineup[GMSQUAD_PORTS * GMSQUAD_SIZE];
    u8 kirby_colors[GMSQUAD_PORTS * GMSQUAD_SIZE];
    int n_lineup = 0;
    int n_kirby = 0;
    bool any_coin = false;
    int p, i, j;

    init();
    if (!gmSquad_ModeAllowed() || !gmSquad_Any()) {
        return;
    }
    /* Character select releases every character for a moment on entry so
     * the last match's files are freed rather than moved; adding squads in
     * that moment would keep them alive. Only add while a coin is down. */
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        if (cache->entries[p].char_id != ChKind_None) {
            any_coin = true;
        }
    }
    if (!any_coin) {
        return;
    }

    for (p = 0; p < GMSQUAD_PORTS; p++) {
        CharacterKind coin = cache->entries[p].char_id;
        if (coin < ChKind_Max) {
            lineup[n_lineup++] = coin;
            if (coin == CKind_Kirby) {
                kirby_colors[n_kirby++] = cache->entries[p].color;
            }
        }
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            GmSquadMember* m = &gmSquad_rows[p][i];
            if (m->ckind >= ChKind_Max) {
                continue;
            }
            Player_80031CB0(m->ckind, m->color);
            lineup[n_lineup++] = m->ckind;
            if (m->ckind == CKind_Kirby) {
                kirby_colors[n_kirby++] = m->color;
            }
        }
    }

    /* Every Kirby can copy anyone in either lineup; the preloader ignores
     * repeated requests. */
    for (j = 0; j < n_kirby; j++) {
        for (i = 0; i < n_lineup; i++) {
            if (lineup[i] != CKind_Kirby) {
                Player_80031D2C(lineup[i], kirby_colors[j]);
            }
        }
    }
}

/* ---- match ------------------------------------------------------------- */

void gmSquad_Disable(void)
{
    s_live = false;
    memset(s_held, 0, sizeof(s_held));
    s_respawning = false;
}

void gmSquad_OnEnterVs(StartMeleeData* start)
{
    int players = 0;
    bool any = false;
    int i;

    gmSquad_Disable();
    init();
    if (!gmSquad_ModeAllowed() || start->rules.match_kind != MatchKind_Stock) {
        return;
    }
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (start->players[i].slot_type == Gm_PKind_NA) {
            continue;
        }
        players++;
        if (i < GMSQUAD_PORTS && gmSquad_Count(i) != 0) {
            any = true;
        }
    }
    if (!any || players != 2) {
        return;
    }
    s_live = true;
    s_stocks = (s8) gmSquad_StocksFor();
    memset(s_lineup, GMSQUAD_NONE, sizeof(s_lineup));
    memset(s_lineup_count, 0, sizeof(s_lineup_count));
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        int k;
        if (start->players[i].slot_type == Gm_PKind_NA) {
            continue;
        }
        start->players[i].stocks = s_stocks;
        if (i >= GMSQUAD_PORTS) {
            continue;
        }
        s_lineup[i][0].ckind = (u8) start->players[i].ckind;
        s_lineup[i][0].color = start->players[i].color;
        s_lineup_count[i] = 1;
        for (k = 0; k < GMSQUAD_EXTRA; k++) {
            if (gmSquad_rows[i][k].ckind < ChKind_Max) {
                s_lineup[i][s_lineup_count[i]++] = gmSquad_rows[i][k];
            }
        }
    }
}

bool gmSquad_StockIconFrame(int slot, int i, f32* frame)
{
    StaticPlayer* ps;
    int index;
    GmSquadMember* m;

    if (!s_live || slot < 0 || slot >= GMSQUAD_PORTS || i <= 0 ||
        s_lineup_count[slot] <= 1)
    {
        return false; /* the fighter out now: the game's own icon */
    }
    ps = Player_GetPtrForSlot(slot);
    /* Stock i from the left is lost after i more KOs: whoever fights then. */
    index = (s_stocks - ps->stocks) / GMSQUAD_STOCKS_PER_FIGHTER + i;
    if (index >= s_lineup_count[slot]) {
        index = s_lineup_count[slot] - 1;
    }
    if (index < 0) {
        return false;
    }
    m = &s_lineup[slot][index];
    if (m->ckind >= ChKind_Max) {
        return false;
    }
    *frame = gm_80168B34(m->ckind, 0, m->color);
    return true;
}

/* What Fighter_Create loads for this fighter, so its archives are parsed now
 * (during setup, which netplay never rolls back) instead of at the swap.
 * Re-parsing an already relocated archive after a rollback would corrupt
 * it; these loaders cache their results and do nothing the second time. */
static void warm_fighter(s32 kind, u8 color)
{
    if (kind < 0) {
        return;
    }
    ftData_8008572C(kind);
    efAsync_LoadSync(ftData_UnkBytePerCharacter[kind]);
    ftData_80085820(kind, color);
    ftData_80085A14(kind);
}

static void warm_copy(s32 kind, u8 kirby_color)
{
    if (kind >= 0 && kind != Ft_Kind_Kirby) {
        Player_80031DA8(kind, kirby_color);
    }
}

void gmSquad_WarmFiles(void)
{
    CharacterKind lineup[GMSQUAD_PORTS][GMSQUAD_SIZE];
    u8 colors[GMSQUAD_PORTS][GMSQUAD_SIZE];
    int count[GMSQUAD_PORTS];
    int p, i, q, k;

    if (!s_live) {
        return;
    }
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        count[p] = 0;
        if (Player_GetPlayerSlotType(p) == Gm_PKind_NA) {
            continue;
        }
        lineup[p][count[p]] = Player_GetPlayerCharacter(p);
        colors[p][count[p]] = Player_GetCostumeId(p);
        count[p]++;
        for (i = 0; i < GMSQUAD_EXTRA; i++) {
            GmSquadMember* m = &gmSquad_rows[p][i];
            if (m->ckind < ChKind_Max) {
                s32 extra;
                s32 kind = Player_PcFighterKinds(m->ckind, &extra);
                warm_fighter(kind, m->color);
                warm_fighter(extra, m->color);
                lineup[p][count[p]] = m->ckind;
                colors[p][count[p]] = m->color;
                count[p]++;
            }
        }
    }
    /* Kirby copies: each Kirby (in its colour) copies anyone on the other
     * side, whoever is on stage when it swallows them. */
    for (p = 0; p < GMSQUAD_PORTS; p++) {
        for (i = 0; i < count[p]; i++) {
            if (lineup[p][i] != CKind_Kirby) {
                continue;
            }
            for (q = 0; q < GMSQUAD_PORTS; q++) {
                if (q == p) {
                    continue;
                }
                for (k = 0; k < count[q]; k++) {
                    s32 extra;
                    s32 kind = Player_PcFighterKinds(lineup[q][k], &extra);
                    warm_copy(kind, colors[p][i]);
                    warm_copy(extra, colors[p][i]);
                }
            }
        }
    }
}

/* The squad member due now for this player, or NULL to respawn as is. */
static GmSquadMember* due_member(s32 slot)
{
    StaticPlayer* ps;
    int lost;
    int index;
    GmSquadMember* m;

    if (!s_live || slot < 0 || slot >= GMSQUAD_PORTS) {
        return NULL;
    }
    ps = Player_GetPtrForSlot(slot);
    if (ps->pkind != Gm_PKind_Human && ps->pkind != Gm_PKind_Cpu) {
        return NULL;
    }
    if (ps->player_state != 2 || ps->stocks <= 0) {
        return NULL;
    }
    lost = s_stocks - ps->stocks;
    if (lost <= 0 || lost % GMSQUAD_STOCKS_PER_FIGHTER != 0) {
        return NULL;
    }
    index = lost / GMSQUAD_STOCKS_PER_FIGHTER - 1;
    if (index < 0 || index >= GMSQUAD_EXTRA) {
        return NULL;
    }
    m = &gmSquad_rows[slot][index];
    if (m->ckind >= ChKind_Max || m->ckind == ps->ckind) {
        return NULL;
    }
    return m;
}

bool gmSquad_HoldRespawn(s32 slot, s32 subchar)
{
    if (s_respawning || subchar != 0 || due_member(slot) == NULL) {
        return false;
    }
    /* Don't swap here: this can run inside the dying fighter's own
     * callbacks, and the swap frees that fighter. The next frame's
     * gmSquad_OnVsFrame does it from the scene. */
    s_held[slot] = true;
    return true;
}

static void swap_in(s32 slot, GmSquadMember* m)
{
    StaticPlayer* ps = Player_GetPtrForSlot(slot);

    Player_80031EBC(slot); /* free the fighter that's out (and Nana/Sheik) */
    ps->player_entity[0] = NULL;
    ps->player_entity[1] = NULL;
    ps->transformed[0] = 0; /* not transformed: Zelda, not Sheik */
    ps->transformed[1] = 1;
    ps->flags.b2 = false; /* "has a second fighter": the new one decides */

    Player_SetPlayerCharacter(slot, m->ckind);
    Player_SetCostumeId(slot, m->color);
    Player_80031AD0(slot); /* create the new fighter(s) */
    fn_8016E124();         /* Kirby copies for everyone now present */
    Player_80031848(slot);

    if (m->ckind == CKind_PopoNana) {
        /* The game only revives a partner that is asleep, and a new Nana
         * isn't: put her to sleep so she drops in beside Popo. */
        HSD_GObj* nana = ps->player_entity[ps->transformed[1]];
        if (nana != NULL) {
            ftCo_800D4F24(nana, 0);
        }
    }
}

void gmSquad_OnVsFrame(void)
{
    s32 slot;

    if (!s_live) {
        return;
    }
    for (slot = 0; slot < GMSQUAD_PORTS; slot++) {
        GmSquadMember* m;
        if (!s_held[slot]) {
            continue;
        }
        s_held[slot] = false;
        m = due_member(slot);
        if (m != NULL) {
            swap_in(slot, m);
        }
        s_respawning = true;
        fn_8016719C(slot, 0);
        s_respawning = false;
    }
}

#endif
