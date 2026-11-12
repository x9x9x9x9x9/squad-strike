/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The lobby of a party: three or four computers, one player each, that end
 * up in one rollback session (docs/party-plan.md).
 *
 * One player hosts and gives out their connect code; the others call it the
 * way they call anyone (Direct Connect, net_match.c). A caller that reaches
 * a hosting party joins it instead of pairing. When the host starts, the
 * guests try to reach each other directly on the socket they reached the
 * host with, everybody measures the trip to everybody else, and the host
 * settles the shape: every pair that got through talks directly, a pair
 * that did not goes through the computer that makes its trip shortest. The
 * socket then goes to net.c (pc_net_connect_party) and the session's own
 * handshake agrees the start frame. */
#ifndef PC_NET_PARTY_H
#define PC_NET_PARTY_H
#include "net_dht.h"
#include "net_identity.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum PcPartyState {
    PC_PARTY_OFF,
    PC_PARTY_LOBBY,   /* gathering; the host may start with 3 or 4 */
    PC_PARTY_PUNCH,   /* guests reaching each other */
    PC_PARTY_CONNECT, /* the session is up, agreeing the start frame */
    PC_PARTY_READY,   /* enter character select at pc_party_start_frame() */
    PC_PARTY_FAIL
};
#define PC_PARTY_MAX 4
typedef struct PcPartyView {
    int size, seat;                         /* computers in it; ours */
    char code[PC_PARTY_MAX][PC_CODE_SIZE];  /* by seat, [0] the host */
    int ping_ms[PC_PARTY_MAX];              /* our trip to each, -1 unknown */
    int8_t via[PC_PARTY_MAX][PC_PARTY_MAX]; /* -1 direct, else the relay's seat */
    bool shaped;                            /* via is final */
} PcPartyView;

/* Host a party on our own code. False (pc_party_state gives why) when the
 * identity or the socket is unavailable. */
bool pc_party_host(void);
/* Host: lock the party and start (3 or 4 computers). */
bool pc_party_start(void);
void pc_party_stop(void);
/* Driven by pc_net_match_poll(), the one call a lobby screen makes. */
void pc_party_poll(void);
int pc_party_state(const char** why);
bool pc_party_active(void);
bool pc_party_is_host(void);
void pc_party_view(PcPartyView* out);
int32_t pc_party_start_frame(void);
uint32_t pc_party_seed(void);

/* MELEE_PARTY_TEST (net_party.c): the lobby with no menu, before the game
 * starts. */
bool pc_party_test(const char* spec);

/* For net_match.c while it is calling a code: an address the code may be at
 * (ask whether a party is hosted there), and a datagram that is not one of
 * its own (true when it was a party's, which may have taken the call
 * over: pc_party_active() then). */
void pc_party_calling(
    const PcNetIdentity* identity, const uint8_t compatibility[20], const char* target_code);
void pc_party_candidate(const struct pc_dht_endpoint* endpoint);
bool pc_party_datagram(const void* data, size_t size, const struct pc_dht_endpoint* from);
#ifdef __cplusplus
}
#endif
#endif
