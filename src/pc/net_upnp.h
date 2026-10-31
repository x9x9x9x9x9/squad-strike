/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_NET_UPNP_H
#define PC_NET_UPNP_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UPnP-IGD port mapping for the internet pairing socket (net_match.c).
 *
 * A PC behind a strict NAT -- a phone hotspot's carrier NAT is the usual one
 * -- sends from a port nobody can predict, so a home router that only lets
 * in replies to what it sent drops it, and hole punching fails. If the home
 * router opens the port outright (most consumer routers allow UPnP), packets
 * from any source get in and the call connects. No server is involved: the
 * request goes to the router on the local network.
 *
 * Everything runs on its own thread; these calls never block. A router
 * without UPnP (or a phone hotspot) simply never reports a mapping. */

/* Ask the router to forward UDP `port` (external = internal) to this PC.
 * Replaces any request still running for an older port. */
void pc_upnp_map_start(uint16_t port);
/* True once the router accepted; the router's public address (network
 * order) and the external port it forwards. */
bool pc_upnp_mapped(uint32_t* external_address, uint16_t* external_port);
/* Remove the mapping (best effort, in the background). Leases also expire
 * on their own after an hour. */
void pc_upnp_unmap(void);
#ifdef __cplusplus
}
#endif
#endif
