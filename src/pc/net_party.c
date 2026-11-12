/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The lobby of a party (net_party.h, docs/party-plan.md). Four messages on
 * the pairing socket, each signed with its sender's identity key:
 *
 *   J  join    caller -> an address the host's code may be at
 *   W  roster  host -> each guest, four times a second: who sits where and
 *              at what address, the phase, and once settled the shape
 *   S  status  guest -> host, in answer: which guests it has reached and
 *              the trip to each computer
 *   P  punch   guest <-> guest while the host says PUNCH: opens both
 *              routers for the pair and times the trip
 *
 * The host is reachable by everyone by construction (they joined through
 * it), so it can always relay a pair that cannot reach each other; another
 * guest is chosen instead when the trip through it is shorter. */
#include "net_party.h"
#include "net.h"
#include "net_lan.h"
#include "net_match.h"
#include "pc.h"
#include <SDL3/SDL_timer.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET PartySocket;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int PartySocket;
#endif

#define PARTY_MAGIC 0x4d505031u /* MPP1 */
#define PARTY_VERSION 1
#define ROSTER_MS 250      /* host: between rosters to one guest */
#define JOIN_MS 500        /* caller: between joins to one address */
#define PUNCH_MS 100       /* guest: between punches to one guest */
#define PUNCH_MIN_MS 1200  /* punch at least this long: the trips need samples */
#define PUNCH_MAX_MS 5000  /* a pair not through by now is relayed */
#define GO_MAX_MS 2000     /* host: wait this long for every guest to take the shape */
#define SILENT_MS 6000     /* a member unheard for this long is gone */
#define READY_BARRIER 0x11 /* reliable, both ways on every host link (as net_match.c) */
#define CANDIDATES 16
enum { PH_LOBBY, PH_PUNCH, PH_GO, PH_CLOSED, PH_LEAVE };

#pragma pack(push, 1)
typedef struct PartyJoin {
    uint32_t magic;
    uint8_t version, type, reserved[2];
    uint64_t nonce;
    uint8_t key[32], compatibility[20];
    char code[PC_CODE_SIZE], target[PC_CODE_SIZE];
    uint32_t local_ip;   /* network order; 0 unknown */
    uint16_t local_port; /* network order */
    uint8_t signature[64];
} PartyJoin;
typedef struct PartySeat {
    char code[PC_CODE_SIZE];
    uint8_t key[32];
    uint32_t ip, local_ip;     /* network order */
    uint16_t port, local_port; /* network order */
} PartySeat;
typedef struct PartyRoster {
    uint32_t magic;
    uint8_t version, type, phase, size;
    uint8_t seat; /* the receiver's */
    uint8_t reserved[3];
    uint64_t party, nonce; /* nonce: the receiver's join nonce */
    PartySeat seats[PC_PARTY_MAX];
    int8_t via[PC_PARTY_MAX][PC_PARTY_MAX];
    uint32_t seed;
    uint32_t echo_ms; /* the stamp of the receiver's last status */
    uint8_t signature[64];
} PartyRoster;
typedef struct PartyStatus {
    uint32_t magic;
    uint8_t version, type, phase, seat;
    uint64_t party;
    uint8_t reach; /* seats whose punch came back */
    uint8_t reserved;
    uint16_t rtt_ms[PC_PARTY_MAX]; /* network order; 0xFFFF unknown */
    uint32_t stamp_ms;
    uint8_t signature[64];
} PartyStatus;
typedef struct PartyPunch {
    uint32_t magic;
    uint8_t version, type, from, to;
    uint64_t party, nonce, echo; /* echo: the receiver's nonce, once heard */
    uint32_t stamp_ms, echo_ms;
    uint8_t signature[64];
} PartyPunch;
#pragma pack(pop)

typedef struct Member {
    PartySeat seat;
    struct pc_dht_endpoint ep; /* where its datagrams come from */
    uint64_t nonce;            /* host: its join nonce */
    uint64_t heard_ms, sent_ms;
    /* host: its last status */
    uint8_t phase, reach;
    uint16_t rtt[PC_PARTY_MAX];
    uint32_t stamp;
    /* guest: the pair with this seat */
    uint64_t my_nonce, its_nonce;
    bool reached;
    uint32_t its_stamp;
    int rtt_ms; /* -1 unknown */
} Member;

static const PcNetIdentity* s_id;
static uint8_t s_compat[20];
static int s_state = PC_PARTY_OFF;
static const char* s_why;
static bool s_host, s_calling;
static char s_target[PC_CODE_SIZE];
static uint64_t s_party, s_join_nonce;
static int s_size, s_seat;
static Member s_m[PC_PARTY_MAX];
static int8_t s_via[PC_PARTY_MAX][PC_PARTY_MAX];
static bool s_shaped;
static uint32_t s_seed;
static int32_t s_start_frame = -1;
static uint64_t s_phase_ms; /* when the current phase began */
static uint8_t s_phase;
static struct {
    struct pc_dht_endpoint ep;
    uint64_t sent_ms;
} s_cand[CANDIDATES];
static int s_cand_n;
static uint32_t s_local_ip;
static bool s_handshake_done, s_barrier_sent[PC_PARTY_MAX], s_barrier_in[PC_PARTY_MAX];
static uint8_t s_go_acked; /* host: seats that answered the shape */
static PartyRoster s_go;   /* guest: the roster the session was built from */
static struct pc_dht_endpoint s_host_ep;
/* MELEE_PARTY_BLOCK=13,23: pairs whose punches are dropped, to test the
 * relay without two routers that refuse each other. */
static uint8_t s_block[PC_PARTY_MAX];

static uint32_t now32(void) {
    return (uint32_t)SDL_GetTicks();
}

static void sign(void* p, size_t n) {
    pc_identity_sign(s_id, (uint8_t*)p + n - 64, p, n - 64);
}

static bool signed_by(const uint8_t key[32], const void* p, size_t n) {
    return pc_identity_verify(key, (const uint8_t*)p + n - 64, p, n - 64);
}

static void send_to(const void* p, size_t n, const struct pc_dht_endpoint* ep) {
    if (ep->address == 0 || ep->port == 0) {
        return;
    }
    if (pc_net_active()) {
        pc_net_send_datagram(p, n, ep->address, ep->port);
        return;
    }
    intptr_t fd = pc_dht_socket();
    if (fd < 0) {
        return;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = ep->address;
    to.sin_port = htons(ep->port);
    sendto((PartySocket)fd, (const char*)p, (int)n, 0, (struct sockaddr*)&to, sizeof to);
}

/* The address this computer has on its own network: the one a UDP socket
 * would send from. Nothing is sent. */
static uint32_t local_ipv4(void) {
    PartySocket s = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return 0;
    }
#else
    if (s < 0) {
        return 0;
    }
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &a.sin_addr);
    uint32_t ip = 0;
#ifdef _WIN32
    int len = sizeof a;
#else
    socklen_t len = sizeof a;
#endif
    if (connect(s, (struct sockaddr*)&a, sizeof a) == 0 &&
        getsockname(s, (struct sockaddr*)&a, &len) == 0)
    {
        ip = a.sin_addr.s_addr;
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return ip;
}

static void fail(const char* why) {
    s_state = PC_PARTY_FAIL;
    s_why = why;
    pc_log_line("party: failed: %s", why);
}

static void reset(void) {
    s_state = PC_PARTY_OFF;
    s_why = NULL;
    s_host = s_calling = s_shaped = s_handshake_done = false;
    s_target[0] = '\0';
    s_size = s_seat = 0;
    s_cand_n = 0;
    s_seed = 0;
    s_start_frame = -1;
    s_phase = PH_LOBBY;
    s_go_acked = 0;
    memset(s_m, 0, sizeof s_m);
    memset(s_via, -1, sizeof s_via);
    memset(s_barrier_sent, 0, sizeof s_barrier_sent);
    memset(s_barrier_in, 0, sizeof s_barrier_in);
    memset(s_block, 0, sizeof s_block);
    const char* b = getenv("MELEE_PARTY_BLOCK");
    for (; b != NULL && b[0] >= '0' && b[0] <= '3' && b[1] >= '0' && b[1] <= '3';) {
        s_block[b[0] - '0'] |= (uint8_t)(1u << (b[1] - '0'));
        s_block[b[1] - '0'] |= (uint8_t)(1u << (b[0] - '0'));
        b += b[2] == ',' ? 3 : 2;
    }
}

static void set_phase(uint8_t phase) {
    s_phase = phase;
    s_phase_ms = SDL_GetTicks();
}

/* ---- host ------------------------------------------------------------ */

static void roster_send(int seat) {
    PartyRoster r;
    memset(&r, 0, sizeof r);
    r.magic = htonl(PARTY_MAGIC);
    r.version = PARTY_VERSION;
    r.type = 'W';
    r.phase = s_phase;
    r.size = (uint8_t)s_size;
    r.seat = (uint8_t)seat;
    r.party = s_party;
    r.nonce = s_m[seat].nonce;
    for (int i = 0; i < s_size; i++) {
        r.seats[i] = s_m[i].seat;
    }
    memcpy(r.via, s_via, sizeof r.via);
    r.seed = htonl(s_seed);
    r.echo_ms = htonl(s_m[seat].stamp);
    sign(&r, sizeof r);
    send_to(&r, sizeof r, &s_m[seat].ep);
    s_m[seat].sent_ms = SDL_GetTicks();
}

static void host_drop(int seat, const char* why) {
    pc_log_line("party: %s %s (seat %d)", s_m[seat].seat.code, why, seat);
    PartyRoster r;
    memset(&r, 0, sizeof r);
    r.magic = htonl(PARTY_MAGIC);
    r.version = PARTY_VERSION;
    r.type = 'W';
    r.phase = PH_CLOSED;
    r.seat = (uint8_t)seat;
    r.party = s_party;
    r.nonce = s_m[seat].nonce;
    sign(&r, sizeof r);
    send_to(&r, sizeof r, &s_m[seat].ep);
    for (int i = seat; i + 1 < s_size; i++) {
        s_m[i] = s_m[i + 1];
    }
    s_size--;
    memset(&s_m[s_size], 0, sizeof s_m[0]);
}

static void host_join(const PartyJoin* j, const struct pc_dht_endpoint* ep) {
    if (memcmp(j->compatibility, s_compat, 20) != 0 || !memchr(j->code, 0, sizeof j->code) ||
        !pc_identity_code_valid(j->code) || strcmp(j->target, s_id->code) != 0 ||
        !signed_by(j->key, j, sizeof *j))
    {
        return;
    }
    for (int i = 1; i < s_size; i++) {
        if (memcmp(s_m[i].seat.key, j->key, 32) == 0) {
            if (s_phase == PH_LOBBY && j->nonce != s_m[i].nonce) {
                /* The same player calling again: the old seat is stale. */
                s_m[i].nonce = j->nonce;
                s_m[i].ep = *ep;
                s_m[i].seat.ip = ep->address;
                s_m[i].seat.port = htons(ep->port);
                s_m[i].seat.local_ip = j->local_ip;
                s_m[i].seat.local_port = j->local_port;
            }
            s_m[i].heard_ms = SDL_GetTicks();
            return;
        }
    }
    if (s_phase != PH_LOBBY || s_size >= PC_PARTY_MAX) {
        return;
    }
    Member* m = &s_m[s_size];
    memset(m, 0, sizeof *m);
    memcpy(m->seat.code, j->code, sizeof m->seat.code);
    memcpy(m->seat.key, j->key, 32);
    m->seat.ip = ep->address;
    m->seat.port = htons(ep->port);
    m->seat.local_ip = j->local_ip;
    m->seat.local_port = j->local_port;
    m->ep = *ep;
    m->nonce = j->nonce;
    m->heard_ms = SDL_GetTicks();
    memset(m->rtt, 0xFF, sizeof m->rtt);
    uint32_t ip = ntohl(ep->address);
    pc_log_line("party: %s joined from %u.%u.%u.%u:%u (seat %d)", m->seat.code, ip >> 24,
        (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, ep->port, s_size);
    s_size++;
    roster_send(s_size - 1);
}

static void host_status(const PartyStatus* st, const struct pc_dht_endpoint* ep) {
    int seat = st->seat;
    if (seat < 1 || seat >= s_size || st->party != s_party ||
        !signed_by(s_m[seat].seat.key, st, sizeof *st))
    {
        return;
    }
    Member* m = &s_m[seat];
    if (st->phase == PH_LEAVE) {
        if (s_phase == PH_LOBBY) {
            host_drop(seat, "left");
        } else if (s_state != PC_PARTY_CONNECT && s_state != PC_PARTY_READY) {
            fail("A player left while the party was connecting");
        }
        return;
    }
    m->ep = *ep;
    m->heard_ms = SDL_GetTicks();
    m->phase = st->phase;
    m->reach = st->reach;
    for (int i = 0; i < PC_PARTY_MAX; i++) {
        m->rtt[i] = ntohs(st->rtt_ms[i]);
    }
    bool fresh = st->stamp_ms != htonl(m->stamp);
    m->stamp = ntohl(st->stamp_ms);
    if (st->phase == PH_GO) {
        s_go_acked |= (uint8_t)(1u << seat);
    }
    if (fresh) {
        roster_send(seat); /* at once: the guest times the trip by it */
    }
}

/* The trip between two seats, ms: a guest's own measurement to the host or
 * to another guest (the mean of the two ends' when both have one). */
static int trip(int a, int b) {
    if (a == b) {
        return 0;
    }
    int x = a != 0 ? s_m[a].rtt[b] : 0xFFFF, y = b != 0 ? s_m[b].rtt[a] : 0xFFFF;
    if (x != 0xFFFF && y != 0xFFFF) {
        return (x + y) / 2;
    }
    return x != 0xFFFF ? x : y != 0xFFFF ? y : 999;
}

static bool pair_direct(int a, int b) {
    if (a == 0 || b == 0) {
        return true;
    }
    return (s_m[a].reach & (1u << b)) != 0 && (s_m[b].reach & (1u << a)) != 0;
}

/* Every pair that reached each other is direct; any other goes through the
 * computer, directly connected to both, that makes its trip shortest. */
static void host_shape(void) {
    memset(s_via, -1, sizeof s_via);
    for (int a = 1; a < s_size; a++) {
        for (int b = a + 1; b < s_size; b++) {
            if (pair_direct(a, b)) {
                continue;
            }
            int best = 0, cost = trip(a, 0) + trip(0, b);
            for (int r = 1; r < s_size; r++) {
                if (r != a && r != b && pair_direct(a, r) && pair_direct(r, b) &&
                    trip(a, r) + trip(r, b) < cost)
                {
                    best = r;
                    cost = trip(a, r) + trip(r, b);
                }
            }
            s_via[a][b] = s_via[b][a] = (int8_t)best;
            pc_log_line("party: %s and %s could not connect directly; going through %s (%d ms)",
                s_m[a].seat.code, s_m[b].seat.code, s_m[best].seat.code, cost);
        }
    }
    s_shaped = true;
}

static void connect_session(void);

static void host_poll(uint64_t now) {
    for (int i = 1; i < s_size; i++) {
        if (now - s_m[i].heard_ms > SILENT_MS) {
            if (s_phase == PH_LOBBY) {
                host_drop(i, "went quiet");
                i--;
                continue;
            }
            fail("A player stopped answering while the party was connecting");
            return;
        }
        if (now - s_m[i].sent_ms >= ROSTER_MS) {
            roster_send(i);
        }
    }
    if (s_phase == PH_PUNCH) {
        bool all = true;
        for (int a = 1; a < s_size; a++) {
            all = all && s_m[a].phase == PH_PUNCH;
            for (int b = a + 1; b < s_size; b++) {
                all = all && pair_direct(a, b);
            }
        }
        uint64_t spent = now - s_phase_ms;
        if ((all && spent >= PUNCH_MIN_MS) || spent >= PUNCH_MAX_MS) {
            host_shape();
            if (!pc_identity_random(&s_seed, sizeof s_seed)) {
                fail("random source failed");
                return;
            }
            set_phase(PH_GO);
            s_go_acked = 0;
            for (int i = 1; i < s_size; i++) {
                roster_send(i);
            }
        }
    } else if (s_phase == PH_GO) {
        uint8_t want = (uint8_t)(((1u << s_size) - 1u) & ~1u);
        if ((s_go_acked & want) == want || now - s_phase_ms >= GO_MAX_MS) {
            connect_session();
        }
    }
}

/* ---- guest ----------------------------------------------------------- */

static void join_send(const struct pc_dht_endpoint* ep) {
    PartyJoin j;
    memset(&j, 0, sizeof j);
    j.magic = htonl(PARTY_MAGIC);
    j.version = PARTY_VERSION;
    j.type = 'J';
    j.nonce = s_join_nonce;
    memcpy(j.key, s_id->public_key, 32);
    memcpy(j.compatibility, s_compat, 20);
    memcpy(j.code, s_id->code, sizeof j.code);
    memcpy(j.target, s_target, sizeof j.target);
    j.local_ip = s_local_ip;
    j.local_port = htons(pc_dht_port());
    sign(&j, sizeof j);
    send_to(&j, sizeof j, ep);
}

static void status_send(uint8_t phase) {
    PartyStatus st;
    memset(&st, 0, sizeof st);
    st.magic = htonl(PARTY_MAGIC);
    st.version = PARTY_VERSION;
    st.type = 'S';
    st.phase = phase;
    st.seat = (uint8_t)s_seat;
    st.party = s_party;
    for (int i = 0; i < PC_PARTY_MAX; i++) {
        if (i < s_size && i != s_seat && s_m[i].reached) {
            st.reach |= (uint8_t)(1u << i);
        }
        int rtt = i < s_size && i != s_seat ? s_m[i].rtt_ms : -1;
        st.rtt_ms[i] = htons((uint16_t)(rtt < 0 ? 0xFFFF : rtt > 60000 ? 60000 : rtt));
    }
    st.stamp_ms = htonl(now32());
    sign(&st, sizeof st);
    send_to(&st, sizeof st, &s_host_ep);
    s_m[0].sent_ms = SDL_GetTicks();
}

static void rtt_note(Member* m, uint32_t echo_ms) {
    if (echo_ms == 0) {
        return;
    }
    uint32_t rtt = now32() - echo_ms;
    if (rtt < 60000 && (m->rtt_ms < 0 || (int)rtt < m->rtt_ms)) {
        m->rtt_ms = (int)rtt;
    }
}

static void punch_send(int to, const struct pc_dht_endpoint* ep) {
    PartyPunch p;
    memset(&p, 0, sizeof p);
    p.magic = htonl(PARTY_MAGIC);
    p.version = PARTY_VERSION;
    p.type = 'P';
    p.from = (uint8_t)s_seat;
    p.to = (uint8_t)to;
    p.party = s_party;
    p.nonce = s_m[to].my_nonce;
    p.echo = s_m[to].its_nonce;
    p.stamp_ms = htonl(now32());
    p.echo_ms = htonl(s_m[to].its_stamp);
    sign(&p, sizeof p);
    send_to(&p, sizeof p, ep);
}

/* Punch a guest at the address the host saw it at; one behind our own
 * router is tried at its address on the local network as well, since few
 * routers turn a packet for their own public address back inside. */
static void punch(int to) {
    Member* m = &s_m[to];
    if (m->reached) {
        punch_send(to, &m->ep);
    } else {
        struct pc_dht_endpoint pub = {m->seat.ip, ntohs(m->seat.port)};
        punch_send(to, &pub);
        if (m->seat.ip == s_m[s_seat].seat.ip && m->seat.local_ip != 0) {
            struct pc_dht_endpoint local = {m->seat.local_ip, ntohs(m->seat.local_port)};
            punch_send(to, &local);
        }
    }
    m->sent_ms = SDL_GetTicks();
}

static void guest_punch(const PartyPunch* p, const struct pc_dht_endpoint* ep) {
    int from = p->from;
    if (s_phase == PH_LOBBY || from < 1 || from >= s_size || from == s_seat || p->to != s_seat ||
        p->party != s_party || (s_block[s_seat] & (1u << from)) != 0 ||
        !signed_by(s_m[from].seat.key, p, sizeof *p))
    {
        return;
    }
    Member* m = &s_m[from];
    bool fresh = m->its_stamp != ntohl(p->stamp_ms);
    m->its_nonce = p->nonce;
    m->its_stamp = ntohl(p->stamp_ms);
    if (p->echo == m->my_nonce) {
        if (!m->reached) {
            uint32_t ip = ntohl(ep->address);
            pc_log_line("party: reached %s directly at %u.%u.%u.%u:%u", m->seat.code, ip >> 24,
                (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, ep->port);
            m->ep = *ep;
        }
        m->reached = true;
        rtt_note(m, ntohl(p->echo_ms));
    }
    if (fresh && !pc_net_active()) {
        punch_send(from, ep); /* at once: the other end times the trip by it */
    }
}

static void guest_roster(const PartyRoster* r, const struct pc_dht_endpoint* ep) {
    if (r->nonce != s_join_nonce || r->size < 2 || r->size > PC_PARTY_MAX || r->seat < 1 ||
        r->seat >= r->size)
    {
        if (s_state != PC_PARTY_OFF && r->nonce == s_join_nonce && r->phase == PH_CLOSED &&
            r->party == s_party && signed_by(s_m[0].seat.key, r, sizeof *r))
        {
            fail("The host closed the party");
        }
        return;
    }
    const PartySeat* host = &r->seats[0];
    if (s_state == PC_PARTY_OFF) {
        /* The first roster: this is the party we were calling. */
        if (!s_calling || !memchr(host->code, 0, sizeof host->code) ||
            strcmp(host->code, s_target) != 0 || !signed_by(host->key, r, sizeof *r))
        {
            return;
        }
        s_m[0].seat = *host;
        s_party = r->party;
        s_host_ep = *ep;
        s_state = PC_PARTY_LOBBY;
        s_phase = PH_LOBBY;
        pc_log_line("party: joined %s's party", host->code);
    } else if (r->party != s_party || ep->address != s_host_ep.address ||
               ep->port != s_host_ep.port || !signed_by(s_m[0].seat.key, r, sizeof *r))
    {
        return;
    }
    s_m[0].heard_ms = SDL_GetTicks();
    rtt_note(&s_m[0], ntohl(r->echo_ms));
    if (s_state == PC_PARTY_CONNECT || s_state == PC_PARTY_READY) {
        if (r->phase == PH_GO) {
            status_send(PH_GO); /* the host missed our answer */
        }
        return;
    }
    if (r->phase == PH_LOBBY || (r->phase == PH_PUNCH && s_phase == PH_LOBBY)) {
        /* Seats move while the lobby is open; once the host says PUNCH they
         * are fixed, and each pair gets a nonce to recognise each other by. */
        s_size = r->size;
        s_seat = r->seat;
        for (int i = 1; i < PC_PARTY_MAX; i++) {
            memset(&s_m[i], 0, sizeof s_m[i]);
            s_m[i].rtt_ms = -1;
            if (i < s_size) {
                s_m[i].seat = r->seats[i];
            }
        }
        if (r->phase == PH_PUNCH) {
            for (int i = 1; i < s_size; i++) {
                if (i != s_seat && !pc_identity_random(&s_m[i].my_nonce, sizeof(uint64_t))) {
                    fail("random source failed");
                    return;
                }
            }
            s_state = PC_PARTY_PUNCH;
            set_phase(PH_PUNCH);
        }
    } else if (r->phase == PH_GO && s_phase != PH_GO) {
        if (s_phase == PH_LOBBY) {
            /* Every PUNCH roster was lost: we reached nobody, and the shape
             * the host settled has all our pairs relayed. */
            s_size = r->size;
            s_seat = r->seat;
            for (int i = 1; i < s_size; i++) {
                s_m[i].seat = r->seats[i];
            }
        } else if (r->size != s_size || r->seat != s_seat) {
            return;
        }
        memcpy(s_via, r->via, sizeof s_via);
        s_seed = ntohl(r->seed);
        s_shaped = true;
        s_go = *r;
        set_phase(PH_GO);
        for (int i = 0; i < 3; i++) {
            status_send(PH_GO);
        }
        connect_session();
    }
}

static void guest_poll(uint64_t now) {
    if (s_state == PC_PARTY_OFF) {
        for (int i = 0; i < s_cand_n; i++) {
            if (now - s_cand[i].sent_ms >= JOIN_MS) {
                s_cand[i].sent_ms = now;
                join_send(&s_cand[i].ep);
            }
        }
        return;
    }
    if (now - s_m[0].heard_ms > SILENT_MS) {
        fail("Lost the party's host");
        return;
    }
    if (now - s_m[0].sent_ms >= ROSTER_MS) {
        status_send(s_phase);
    }
    if (s_phase == PH_PUNCH) {
        for (int i = 1; i < s_size; i++) {
            if (i != s_seat && (s_block[s_seat] & (1u << i)) == 0 &&
                now - s_m[i].sent_ms >= PUNCH_MS)
            {
                punch(i);
            }
        }
    }
}

/* ---- the session ----------------------------------------------------- */

/* Lobby datagrams that arrive once net.c has the socket (its receive
 * thread): a roster from a host that missed our answer gets it again. */
static bool after_handoff(const void* data, size_t n, uint32_t address, uint16_t port) {
    const PartyRoster* r = data;
    if (n < 8 || ntohl(r->magic) != PARTY_MAGIC) {
        return false;
    }
    if (!s_host && n == sizeof *r && r->type == 'W' && r->phase == PH_GO &&
        address == s_host_ep.address && port == s_host_ep.port &&
        memcmp(r, &s_go, sizeof *r - sizeof r->signature - sizeof r->echo_ms) == 0)
    {
        PartyStatus st;
        memset(&st, 0, sizeof st);
        st.magic = htonl(PARTY_MAGIC);
        st.version = PARTY_VERSION;
        st.type = 'S';
        st.phase = PH_GO;
        st.seat = (uint8_t)s_seat;
        st.party = s_party;
        memset(st.rtt_ms, 0xFF, sizeof st.rtt_ms);
        sign(&st, sizeof st);
        pc_net_send_datagram(&st, sizeof st, address, port);
    }
    return true; /* never the session's */
}

static void connect_session(void) {
    PcNetPartySeat seats[4];
    memset(seats, 0, sizeof seats);
    for (int i = 0; i < s_size; i++) {
        if (i == s_seat) {
            continue;
        }
        const Member* m = &s_m[i];
        bool direct = s_via[s_seat][i] < 0;
        if (s_host || i == 0) {
            seats[i].address = s_host ? m->ep.address : s_host_ep.address;
            seats[i].port = s_host ? m->ep.port : s_host_ep.port;
        } else if (direct) {
            if (!m->reached) {
                fail("The party's shape does not match what this computer reached");
                return;
            }
            seats[i].address = m->ep.address;
            seats[i].port = m->ep.port;
            /* A guest pair has no handshake of its own to key its link:
             * the two punch nonces, which only the pair and the signatures
             * on them vouch for, do it. Lower seat first on both ends. */
            uint64_t lo = s_seat < i ? m->my_nonce : m->its_nonce;
            uint64_t hi = s_seat < i ? m->its_nonce : m->my_nonce;
            snprintf(seats[i].secret, sizeof seats[i].secret, "%016llx%016llx%016llx",
                (unsigned long long)s_party, (unsigned long long)lo, (unsigned long long)hi);
        }
    }
    intptr_t fd = pc_dht_take_socket();
    if (!pc_net_connect_party(fd, s_seat, s_size, seats, s_via, s_seed)) {
        if (fd != -1) {
#ifdef _WIN32
            closesocket((SOCKET)fd);
#else
            close((int)fd);
#endif
        }
        fail("connection failed");
        return;
    }
    memset(seats, 0, sizeof seats);
    pc_net_set_datagram_handler(after_handoff);
    s_state = PC_PARTY_CONNECT;
    pc_log_line("party: session up, seat %d of %d", s_seat, s_size);
}

/* The session's own handshake, then a barrier message both ways on every
 * link of the host's so nobody enters character select on a start frame
 * another never heard (as net_match.c does for two). */
static void connect_poll(void) {
    pc_net_poll();
    if (!s_handshake_done) {
        s_handshake_done = s_host ? pc_net_host_match(s_seed, &s_start_frame) :
                                    pc_net_guest_wait_match(&s_seed, &s_start_frame);
    }
    if (pc_net_handshake_state() == 3) {
        fail("match handshake failed");
        return;
    }
    if (!s_handshake_done) {
        return;
    }
    int links = s_host ? pc_net_party_links() : 1;
    bool all = true;
    for (int i = 0; i < links; i++) {
        pc_net_party_use_link(i);
        if (!s_barrier_sent[i]) {
            s_barrier_sent[i] = pc_net_send_reliable(READY_BARRIER, NULL, 0);
        }
        uint8_t type, buf[256];
        int n;
        while ((n = pc_net_recv_reliable(&type, buf, sizeof buf)) >= 0) {
            if (type == READY_BARRIER && n == 0) {
                s_barrier_in[i] = true;
            }
        }
        all = all && s_barrier_sent[i] && s_barrier_in[i];
    }
    pc_net_party_use_link(0);
    if (!all) {
        return;
    }
    if (pc_net_frame() > s_start_frame) {
        fail("late ready barrier");
        return;
    }
    s_state = PC_PARTY_READY;
    pc_log_line("party: ready, start frame %d", s_start_frame);
}

/* ---- the module's face ------------------------------------------------ */

static void receive(const void* data, size_t n, const struct pc_dht_endpoint* ep) {
    const PartyJoin* j = data;
    if (j->version != PARTY_VERSION) {
        return;
    }
    if (s_host && s_state != PC_PARTY_FAIL) {
        if (j->type == 'J' && n == sizeof(PartyJoin)) {
            host_join(data, ep);
        } else if (j->type == 'S' && n == sizeof(PartyStatus)) {
            host_status(data, ep);
        }
    } else if (!s_host && s_state != PC_PARTY_FAIL) {
        if (j->type == 'W' && n == sizeof(PartyRoster)) {
            guest_roster(data, ep);
        } else if (j->type == 'P' && n == sizeof(PartyPunch) && s_state != PC_PARTY_OFF) {
            guest_punch(data, ep);
        }
    }
}

bool pc_party_datagram(const void* data, size_t size, const struct pc_dht_endpoint* from) {
    if (size < 8 || ntohl(*(const uint32_t*)data) != PARTY_MAGIC) {
        return false;
    }
    if (s_id != NULL && (s_host || s_calling)) {
        receive(data, size, from);
    }
    return true;
}

void pc_party_calling(
    const PcNetIdentity* identity, const uint8_t compatibility[20], const char* target_code) {
    reset();
    if (identity == NULL || target_code == NULL || target_code[0] == '\0' ||
        !pc_identity_random(&s_join_nonce, sizeof s_join_nonce))
    {
        s_id = NULL;
        return;
    }
    s_id = identity;
    memcpy(s_compat, compatibility, 20);
    snprintf(s_target, sizeof s_target, "%s", target_code);
    s_calling = true;
    s_local_ip = local_ipv4();
    for (int i = 0; i < PC_PARTY_MAX; i++) {
        s_m[i].rtt_ms = -1;
    }
    /* MELEE_PARTY_AT=ip:port: where the host is, for a test with no DHT
     * and no multicast to find it by. */
    const char* at = getenv("MELEE_PARTY_AT");
    const char* colon = at != NULL ? strrchr(at, ':') : NULL;
    char ip[64];
    if (colon != NULL && (size_t)(colon - at) < sizeof ip) {
        memcpy(ip, at, (size_t)(colon - at));
        ip[colon - at] = '\0';
        struct pc_dht_endpoint ep = {0, (uint16_t)atoi(colon + 1)};
        if (inet_pton(AF_INET, ip, &ep.address) == 1) {
            pc_party_candidate(&ep);
        }
    }
}

void pc_party_candidate(const struct pc_dht_endpoint* ep) {
    if (!s_calling || s_state != PC_PARTY_OFF) {
        return;
    }
    for (int i = 0; i < s_cand_n; i++) {
        if (s_cand[i].ep.address == ep->address && s_cand[i].ep.port == ep->port) {
            return;
        }
    }
    int at = s_cand_n < CANDIDATES ? s_cand_n++ : (int)(SDL_GetTicks() % CANDIDATES);
    s_cand[at].ep = *ep;
    s_cand[at].sent_ms = SDL_GetTicks();
    join_send(ep);
}

bool pc_party_host(void) {
    pc_party_stop();
    reset();
    const uint8_t* compat = NULL;
    if (!pc_net_match_party_host(&s_id, &compat)) {
        s_id = NULL;
        fail("Could not open a party (identity or network unavailable)");
        return false;
    }
    memcpy(s_compat, compat, 20);
    if (!pc_identity_random(&s_party, sizeof s_party)) {
        fail("random source failed");
        return false;
    }
    s_host = true;
    s_size = 1;
    s_seat = 0;
    memcpy(s_m[0].seat.code, s_id->code, sizeof s_m[0].seat.code);
    memcpy(s_m[0].seat.key, s_id->public_key, 32);
    s_state = PC_PARTY_LOBBY;
    set_phase(PH_LOBBY);
    pc_log_line("party: hosting as %s", s_id->code);
    return true;
}

bool pc_party_start(void) {
    if (!s_host || s_state != PC_PARTY_LOBBY || s_size < 3) {
        return false;
    }
    s_state = PC_PARTY_PUNCH;
    set_phase(PH_PUNCH);
    for (int i = 1; i < s_size; i++) {
        s_m[i].phase = PH_LOBBY;
        s_m[i].reach = 0;
        roster_send(i);
    }
    pc_log_line("party: starting with %d", s_size);
    return true;
}

void pc_party_stop(void) {
    if (s_id != NULL && s_state != PC_PARTY_OFF && s_state != PC_PARTY_FAIL &&
        s_state != PC_PARTY_CONNECT && s_state != PC_PARTY_READY)
    {
        if (s_host) {
            while (s_size > 1) {
                host_drop(s_size - 1, "sent home (party closed)");
            }
        } else {
            status_send(PH_LEAVE);
        }
    }
    bool was = s_state != PC_PARTY_OFF || s_host || s_calling;
    reset();
    if (was) {
        pc_net_set_datagram_handler(NULL);
    }
}

/* Called by pc_net_match_poll, which keeps the socket, the DHT and the
 * local beacon going while the lobby is open. */
void pc_party_poll(void) {
    if (s_id == NULL || s_state == PC_PARTY_FAIL || s_state == PC_PARTY_READY) {
        return;
    }
    if (s_state == PC_PARTY_CONNECT) {
        connect_poll();
        return;
    }
    uint64_t now = SDL_GetTicks();
    if (s_host) {
        host_poll(now);
    } else if (s_calling) {
        guest_poll(now);
    }
}

int pc_party_state(const char** why) {
    if (why != NULL) {
        *why = s_why;
    }
    return s_state;
}

bool pc_party_active(void) {
    return s_state != PC_PARTY_OFF;
}

bool pc_party_is_host(void) {
    return s_host;
}

void pc_party_view(PcPartyView* out) {
    memset(out, 0, sizeof *out);
    out->size = s_size;
    out->seat = s_seat;
    out->shaped = s_shaped;
    memcpy(out->via, s_via, sizeof out->via);
    for (int i = 0; i < PC_PARTY_MAX; i++) {
        out->ping_ms[i] = -1;
        if (i >= s_size) {
            continue;
        }
        memcpy(out->code[i], s_m[i].seat.code, sizeof out->code[i]);
        out->code[i][PC_CODE_SIZE - 1] = '\0';
        if (i == s_seat) {
            continue;
        }
        if (s_host) {
            out->ping_ms[i] = s_m[i].rtt[0] != 0xFFFF && s_m[i].heard_ms != 0 ? s_m[i].rtt[0] : -1;
        } else {
            out->ping_ms[i] = s_m[i].rtt_ms;
        }
    }
}

int32_t pc_party_start_frame(void) {
    return s_start_frame;
}

uint32_t pc_party_seed(void) {
    return s_seed;
}

/* MELEE_PARTY_TEST=host:N | call:CODE: a party through its real lobby
 * (net_party.c) with no menu, before the game starts: host a party and
 * start it once N computers are in, or call the host's code. The session
 * that comes out agrees its match the way a MELEE_NET one does
 * (pc_net_direct_handshake). */
bool pc_party_test(const char* spec) {
    bool host = strncmp(spec, "host:", 5) == 0;
    int want = host ? atoi(spec + 5) : 0;
    if (host ? !pc_party_host() : !pc_net_match_start(PC_MATCH_DIRECT, spec + 5)) {
        pc_log_line("net: MELEE_PARTY_TEST=%s could not start", spec);
        return false;
    }
    uint64_t t0 = SDL_GetTicks();
    while (pc_party_state(NULL) < PC_PARTY_CONNECT) {
        pc_net_match_poll();
        PcPartyView v;
        pc_party_view(&v);
        if (host && pc_party_state(NULL) == PC_PARTY_LOBBY && v.size >= want) {
            pc_party_start();
        }
        if (SDL_GetTicks() - t0 > 90000) {
            pc_log_line("net: MELEE_PARTY_TEST=%s: no party in 90 s", spec);
            return false;
        }
        SDL_Delay(2);
    }
    if (pc_party_state(NULL) != PC_PARTY_CONNECT) {
        return false;
    }
    pc_net_direct_handshake();
    return true;
}
