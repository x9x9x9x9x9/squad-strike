#!/usr/bin/env python3
"""A party session on loopback: one host and two or three guests, each a
separate instance (docs/party-plan.md). Every instance's player mashes by
itself (MELEE_NET_FAKE), in a four-player team match (MELEE_DEBUG_VS=four),
and each writes a per-frame record of the four pads and the checksum. Passes
when no instance reports a DESYNC and all records agree frame for frame,
with every seat's port carrying input.

  tools/net_party_test.py --exe build/melee --disc melee.iso [--guests 3]
      [--minutes 1] [--delay MS] [--jitter] [--loss PCT]

MELEE_BOOT_SCENE=vs is set so the instances go straight to the match; pass
MELEE_BACKEND=null in the environment to run without a GPU."""
import argparse, os, shutil, struct, subprocess, sys, time

KEY = "melee-pc party fixture key"
REC_HDR, REC_STRIDE = 8, 76  # net_snapshot.c record_frame: 4 PADStatus + ck + pad


def records(path):
    data = open(path, "rb").read()
    n = (len(data) - REC_HDR) // REC_STRIDE
    return [data[REC_HDR + i * REC_STRIDE: REC_HDR + i * REC_STRIDE + 68] for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--work", default="/tmp/melee_party")
    ap.add_argument("--port", type=int, default=41400)
    ap.add_argument("--guests", type=int, default=3, choices=(2, 3))
    ap.add_argument("--minutes", type=float, default=1.0)
    ap.add_argument("--delay", type=int, default=0, help="one-way ms on every link")
    ap.add_argument("--jitter", action="store_true")
    ap.add_argument("--loss", type=int, default=0)
    ap.add_argument("--relay", default="",
                    help="pairs with no direct connection and the seat relaying them, "
                         "e.g. 13:0 or 12:0,13:0 (default: every pair direct)")
    ap.add_argument("--star", action="store_true",
                    help="every guest pair relayed through the host")
    args = ap.parse_args()
    shutil.rmtree(args.work, ignore_errors=True)
    os.makedirs(args.work)
    frames = int(args.minutes * 3600) + 9000
    relay = args.relay
    if args.star:
        relay = ",".join(f"{a}{b}:0" for a in range(1, args.guests + 1)
                         for b in range(a + 1, args.guests + 1))
    names = ["host"] + [f"guest{i}" for i in range(1, args.guests + 1)]
    procs, logs = [], []
    for seat, name in enumerate(names):
        e = dict(os.environ)
        cache = os.path.join(args.work, f"cache{seat}")
        os.makedirs(cache, exist_ok=True)
        e.update({
            "MELEE_VSYNC": "0", "MELEE_BOOT_SCENE": "vs", "MELEE_DEBUG_VS": "four",
            "MELEE_NET_PORT": str(args.port + seat), "MELEE_CACHE_DIR": cache,
            "MELEE_SEED": "7", "MELEE_NET_KEY": KEY, "MELEE_NET_FAKE": "1",
            "MELEE_NET_RECORD": os.path.join(args.work, f"{name}.rec"),
            "MELEE_NET_EXIT_AFTER_FRAMES": str(frames),
        })
        if args.delay:
            e["MELEE_NET_SIM_DELAY_MS"] = str(args.delay)
        if args.jitter:
            e["MELEE_NET_SIM_JITTER_MS"] = "20"
        if args.loss:
            e["MELEE_NET_SIM_LOSS"] = str(args.loss)
        e["MELEE_NET"] = f"127.0.0.1:{args.port + (1 if seat == 0 else 0)}"
        e["MELEE_NET_PLAYER"] = "0" if seat == 0 else "1"
        e["MELEE_NET_SEAT"] = str(seat)
        e["MELEE_NET_PARTY"] = ",".join(
            f"127.0.0.1:{args.port + s}" for s in range(args.guests + 1))
        if relay:
            e["MELEE_NET_PARTY_RELAY"] = relay
        e.pop("MELEE_LOG_FILE", None)
        log = open(os.path.join(args.work, f"{name}.log"), "wb")
        logs.append(log)
        procs.append(subprocess.Popen([args.exe, "--no-card", args.disc], env=e, stdout=log,
                                      stderr=subprocess.STDOUT))
    deadline = time.time() + frames / 12.0 + 120
    while time.time() < deadline and any(p.poll() is None for p in procs):
        time.sleep(1)
    for p in procs:
        if p.poll() is None:
            p.kill()
            p.wait()
    for log in logs:
        log.close()
    ok = True
    recs = []
    for name in names:
        text = open(os.path.join(args.work, f"{name}.log"), errors="replace").read()
        stats = [l for l in text.split("\n") if "net: frame " in l and "rollbacks" in l]
        done = "net: test done" in text
        bad = [l for l in text.split("\n") if "DESYNC" in l or "PANIC" in l or "leaving netplay" in l]
        print(f"[{name}] {'done' if done else 'NOT DONE'}; "
              f"{stats[-1].split(', stalls')[0].split('net: ')[-1] if stats else 'no stats'}")
        for l in bad[:3]:
            print(f"[{name}] {l[:160]}")
        ok = ok and done and not bad
        path = os.path.join(args.work, f"{name}.rec")
        recs.append(records(path) if os.path.exists(path) else [])
    n = min(len(r) for r in recs)
    first_diff = next((f for f in range(n) if any(r[f] != recs[0][f] for r in recs[1:])), None)
    active = [sum(1 for f in range(n) if struct.unpack_from("<H", recs[0][f], p * 16)[0])
              for p in range(4)]
    print(f"records: {n} frames, first difference {first_diff}, frames with buttons per port "
          f"{active}")
    ok = ok and n > 3000 and first_diff is None and all(a > 0 for a in active[:args.guests + 1])
    print("net_party_test:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
