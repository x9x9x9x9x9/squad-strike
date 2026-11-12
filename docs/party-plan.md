# Four computers online ("party" sessions): plan and status

Goal: a 2v2 (or any four-player VS match, Squad Strike included) where every
player is on their own computer. LAN and Direct only, like squads and two
players per computer; matchmaking stays 1v1.

This file is the working plan. Update the status table when a step lands.

## Topology: direct where it can be, relayed where it cannot

Every pair of computers talks directly, so a player's input reaches each of
the other three in one trip. A pair that cannot connect (two routers that
both refuse unsolicited UDP) goes through a third computer that reaches
both; nobody chooses or configures this.

- `net.link[]` is one link per computer this one reaches directly, in seat
  order (`net.link_seat[]`), so a guest's link 0 is always the host.
- Each link's remote stream speaks for the far computer's port
  (`net.own_link[]`), plus the port of any player that computer relays to
  us. `net.relay[]` is the ports we forward on a link from the links that
  own them (`tx_pad`, `tx_newest`: a relayed frame can only go out once its
  owner delivered it).
- `party_shape(seat, size, via)` (net.c) builds all three tables from one
  description every computer holds alike: `via[a][b]` is the seat relaying
  between a and b, or -1. The host is direct to everybody by construction
  (they joined through it), so a relay always exists.
- The host still settles what needs one voice: the rules and start frame
  (RULES/READY with each guest), the frame a scene ends on
  (`scene_hold_host`), and the input delay (`delay_auto`, announced on
  every link).
- Time sync: every link's frame-advantage samples feed the one estimator,
  so each computer paces itself against the middle of the others; no
  computer is the clock. A link that carries a relayed player measures
  against the relay's own newest frame (`sync_have`).

The first version of this branch was a star (everything through the host).
It is still expressible (`tools/net_party_test.py --star`) and is what a
party degrades to when no guest pair can connect.

## The lobby (net_party.c)

One player picks "Host a party" in the Direct Connect hub and gives out
their connect code. The others call that code exactly as they call anyone:
net_match.c asks every address the code is found at whether a party is
hosted there (`pc_party_candidate`), and a caller that gets a roster back
joins the party instead of pairing.

Four signed messages on the pairing socket:

| | from -> to | what |
|---|---|---|
| J join | caller -> host | code, key, address on its own network |
| W roster | host -> guest, 4 Hz | seats, addresses as the host sees them, phase, and once settled the shape and seed |
| S status | guest -> host | which guests it reached, its trip to every computer |
| P punch | guest <-> guest | opens both routers for the pair, times the trip |

When the host presses START (3 or 4 computers) the guests punch each other
at the address the host saw them at (the same socket and so, behind most
routers, the same mapping), and at the local address too when both sit
behind one public address. After at least 1.2 s and at most 5 s the host
shapes the party (`host_shape`): mutual reach is direct; any other pair
goes through whichever computer, direct to both, gives the shortest
measured trip. Then every computer hands its socket to net.c
(`pc_net_connect_party`) and the session's own handshake agrees the start
frame.

Keys: a host-guest link is keyed by the session handshake as in a
two-computer session; a guest-guest link has no handshake, so it is keyed
from the party id and the pair's two signed punch nonces.

On the way into character select every computer opens a door per seat. Four
players start as a team battle, 1+3 against 2+4, except that a relayed pair
starts on the same team (they see each other a little late, which matters
least between teammates). Players can change teams as usual.

## Wire: ports, not peers (step 1)

`WirePad` is one frame of one stream and holds four `WireOne`, indexed by
controller port. Each stream owns a fixed set of ports for the session:

| session            | local stream owns     | remote stream owns        |
|--------------------|-----------------------|---------------------------|
| two computers      | local, local + 2      | remote, remote + 2        |
| party              | its seat              | per link: the far seat, plus seats it relays to us |

A port a stream owns but has no controller for has `WIRE_PRESENT` clear in
its `button`; the queue head gets `PAD_ERR_NO_CONTROLLER` there. The codec
(net_wire.c) delta-codes each `WireOne` with its own mask byte, so an absent
port costs one byte a frame.

`write_head` fills each port from whichever ring owns it. Nothing else in
the rollback core cares how many players a stream carries: prediction,
misprediction detection and resends all treat a `WirePad` as opaque bytes.

## Links (step 2)

Everything that describes one peer is an array over `NET_LINKS` behind a
macro of its old name (`#define s_remote_have (s_remote_have_l[t_li])`);
the thread-local `t_li` is the link the running code works on, so the link
code reads as it did for one peer. The core asks the links in aggregate:
remote input for a frame is each link's ring for the ports it owns; a frame
is confirmed when every link has it; the rollback target is the earliest
misprediction over links.

Desync check: per link. Disconnect: one computer leaving ends the session
for all.

## Status

| step | what | state |
|------|------|-------|
| 1 | port-indexed wire pads, ownership masks; two computers and two per computer unchanged | done |
| 2 | per-link state, a link per direct peer, relay masks, handshake to all guests, a key per link | done |
| 3 | `MELEE_NET_SEAT` / `MELEE_NET_PARTY` / `MELEE_NET_PARTY_RELAY`, `tools/net_party_test.py` | done |
| 4 | every pair direct, relay only for pairs that cannot connect | done |
| 5 | lobby: join by the host's code, punch, measure, shape (net_party.c; `net_party_test.py --lobby [--block 13]`) | done on loopback; not yet run across real routers |
| 6 | party screen and "Host a party" in the Direct hub, a door per seat, default teams, stage chooser | written; not run (the test disc has no menu files) |
| 7 | auto delay for the party (host covers the slowest pair, announces on every link) | done |
| 8 | reconnect window in a party | not started: a silent link still ends the session for everyone |
| 9 | chat in a party | off in a party for now (a message reaches one computer only) |

Measured on loopback (four instances, null backend, `MELEE_DEBUG_VS=four`,
every player a self-mashing pad; 30 ms one way per link, 20 ms jitter, 2%
loss; about 10,400 frames, all four records identical in every run):

| shape | rollback depth, worst computer | rollbacks per computer |
|---|---|---|
| every pair direct | 3 | 90 to 760 |
| one guest pair relayed through another guest | 7 | 360 to 1,970 |
| star (everything through the host), earlier build | 7 | 660 host, about 1,650 each guest |

Known gaps:

- The lobby's connect path after the hand-over (`connect_poll`: handshake
  and ready barrier driven from the lobby scene) is only exercised through
  `MELEE_PARTY_TEST`, which lets the session agree its match the way a
  `MELEE_NET` one does.
- A guest that reaches the host only on its local network (same house as
  the host) is seen by outside guests at an address they cannot use; those
  pairs fall back to the relay.
- The host covers relayed pairs with the trip the lobby measured, not a
  live one.
- One three-computer relayed run stopped with the host's main thread in
  the renderer's frame drain; it did not repeat in two more runs and the
  records to that point agreed. Unexplained.
- The character-select door team table (`mnCharSel_803F0DFC`) outlives the
  scene. A party sets all four doors on entry; a two-computer session does
  not reset it.
