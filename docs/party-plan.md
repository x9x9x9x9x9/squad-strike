# Four computers online ("party" sessions): plan and status

Goal: a 2v2 (or any four-player VS match, Squad Strike included) where every
player is on their own computer. LAN and Direct only, like squads and two
players per computer; matchmaking stays 1v1.

This file is the working plan. Update the status table when a step lands.

## Topology: a star

The host holds one link to each guest; guests hold one link, to the host.

- Only the host has to be reachable (its connect code, UPnP or a forwarded
  port). Guests never talk to each other, so there is no guest-to-guest NAT
  traversal.
- A guest runs the ordinary two-computer session: one remote, one remote
  input stream. The only difference is which ports that stream speaks for.
- The host relays: what it sends guest A for frame f is its own input for f
  plus the other guests' inputs for f, so it can only send A frames that
  every other guest has delivered. A guest therefore sees the other guests
  one extra hop late (their latency to the host plus the host's to it) and
  predicts them meanwhile, like any late remote input.

Rejected: a full mesh (pairwise NAT traversal, three links on every
computer, and all of the link layer instanced everywhere); a separate hub
process speaking the protocol to four unchanged clients (it would have to
re-implement the authenticated handshake, the reliable channel, the scene
hand-off and time sync as the "peer" of each client).

## Wire: ports, not peers (step 1)

`WirePad` is one frame of one stream and holds four `WireOne`, indexed by
controller port. Each stream owns a fixed set of ports for the session:

| session            | local stream owns     | remote stream owns        |
|--------------------|-----------------------|---------------------------|
| two computers      | local, local + 2      | remote, remote + 2        |
| party, guest       | its seat              | every other port          |
| party, host        | port 0                | per link: that guest's seat |

A port a stream owns but has no controller for has `WIRE_PRESENT` clear in
its `button`; the queue head gets `PAD_ERR_NO_CONTROLLER` there. The codec
(net_wire.c) delta-codes each `WireOne` with its own mask byte, so an absent
port costs one byte a frame.

`write_head` fills each port from whichever ring owns it. Nothing else in
the rollback core cares how many players a stream carries: prediction,
misprediction detection and resends all treat a `WirePad` as opaque bytes.

## Host: three links (step 2)

Everything that describes one peer moves from file statics into `struct
Link` (net_internal.h); the host has up to three, a guest or a two-computer
session one. A thread-local `L` names the link the running code is working
on, and the old static names become macros onto it (`#define s_remote_have
(L->remote_have)`), so the link code keeps its text. Per link:

- address, session key, handshake state and nonces, reliable channel;
- remote ring, `remote_have`, `rx_have`, remote checksums;
- what the peer has acked (`last_acked`), tx sequence, RTT ring, loss and
  ping estimates, rx sequence window;
- time-sync estimator, resume state, silence/progress timers, peer-left.

Per session (unchanged statics): the local ring and `s_wrote`, the frame
counter, snapshots, audits, audio journal, pad queue.

The core asks the links in aggregate:

- remote input for frame f = each link's ring for the ports it owns;
- `remote_have` = the minimum over links (a frame is confirmed when every
  guest's input for it is in);
- the rollback target = the earliest misprediction over links;
- stall / time-sync slow-down = the largest any link asks for.

Relay: the frames the host can send link i run up to `min(s_wrote,
remote_have of every other link)`; its tx ring for i is the local ring
merged with the other links' rings.

Handshake: the host runs the existing handshake with each guest and gives
all of them the same seed and start frame; the lobby start fence waits for
all links. RULES are the host's, as now.

Desync check: per link, host against that guest (a guest that parts from
the host has parted from everyone).

Disconnect: one guest leaving ends the session for all (first version).
Resume after an interruption: per link, as now.

## Session start (step 3)

- Test/CLI: `MELEE_NET=host:<port>:<guests>` and the existing
  `MELEE_NET=connect:...` with `MELEE_NET_SEAT=<1..3>`.
  `tools/net_test.py --four` runs four instances on loopback.
- Direct: the host opens a party on its connect code; guests enter the
  host's code. The host assigns seats in arrival order (step 5).
- LAN: the lobby lists a party and how many seats are taken (step 5).

## Time sync (step 4)

Two computers compare frame advantages (`adv`) and the one ahead gives a
frame back. In a star each guest syncs against the host as now, but its
`adv` must be measured against the host's own newest input (`pk->newest`),
not against the relayed stream, which is held back by the slowest guest.
The host takes the largest correction any link asks for.

## Status

| step | what | state |
|------|------|-------|
| 1 | port-indexed wire pads, ownership masks; two computers and two per computer unchanged | done |
| 2a | per-link state as arrays over `NET_LINKS` behind macros of the old names, indexed by the thread-local `t_li` (this replaced the `struct Link` idea above: no types had to move) | done |
| 2b | host with a link per guest: per-link send/receive/timers/gate, aggregate confirmed frame and waits, relay (`tx_newest`, `tx_pad`), handshake to all guests, a key per link | done |
| 3 | `MELEE_NET_PARTY` / `MELEE_NET_SEAT` / `MELEE_NET_PARTY_SIZE`, `tools/net_party_test.py` | done |
| 4 | time sync in a star, tuning with simulated latency | started: see below |
| 5 | Direct/LAN party connect flow and menus | not started |
| 6 | reliable messages on a host (scene hand-off, pause, chat), four doors on character select online, stage chooser, results | not started |

Measured on loopback (four instances, null backend, `MELEE_DEBUG_VS=four`,
every player a self-mashing pad):

- no simulated latency: 10,799 frames, all four records identical, no
  rollbacks needed;
- 30 ms one way per link (about 65 ms ping to the host), 20 ms jitter, 2%
  loss: 10,436 frames, all four records identical, no desync; the host
  rolled back 660 times (depth up to 3), each guest about 1,650 times
  (depth up to 7, which is `WINDOW`: a guest sees the other guests two
  hops late), 80 to 150 short stalls each.

What a party does not have yet (each is a two-computer exchange today):

- **Time sync (step 4).** The host feeds every link's advantage samples
  into the one estimator and guests measure against `pk->newest`
  (`sync_have`). It held in the runs above but is not designed: the host
  should not be steered by its slowest guest. `WINDOW` (7) is also tight
  for guest-to-guest input at ordinary pings.
- **Auto delay.** Fixed at `PARTY_DELAY` (3) or the configured delay.
- **Resume.** `s_rc_window_ms` is 0 in a party: a silent link ends the
  session for everyone.
- **Reliable messages from the host other than the handshake and the
  scene hand-off.** `pc_net_send_reliable` sends on the current link (link 0
  on the game thread). The scene hand-off is done (`scene_hold_host`: the
  host announces the latest frame of all once every guest's is in; measured
  with `MELEE_DEBUG_VS_STOCKS=1`, the match ended and all four computers
  entered the results scene on frame 483). The lobby's ready barrier
  (`REL_READY_BARRIER`, net_lan.c and net_match.c), the delay announcement
  and chat still go to one guest only.
- **Starting one from the menus (step 5)** and everything the online mode
  assumes about two players (step 6): `pc_net_local_player()` is 0 or 1 in
  gmonlinemode.c, the stage chooser, the ping display, rumble's port map.
