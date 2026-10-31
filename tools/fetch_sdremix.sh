#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Puts SD Remix's fighter files (github.com/underscoreglook/SD-Remix, its
# chars/ folder, v3.3) in DEST/sdremix, for the "SD Remix characters"
# setting (src/pc/sdremix.cpp looks for an sdremix folder next to the exe).
# Pinned to one commit and checked against these hashes.
#   usage: bash tools/fetch_sdremix.sh DEST
set -euo pipefail

DEST="${1:?usage: fetch_sdremix.sh DEST}/sdremix"
COMMIT="2458cb1231fb80717370c892535f0495997571bb"
BASE="https://raw.githubusercontent.com/underscoreglook/SD-Remix/${COMMIT}/chars"

mkdir -p "${DEST}"
while read -r sum name; do
    [ -n "${name}" ] || continue
    curl -sSfL --retry 4 --retry-delay 3 -o "${DEST}/${name}" "${BASE}/${name}"
    echo "${sum}  ${DEST}/${name}" | sha256sum -c --quiet -
done <<'HASHES'
80e4be6d18fc2b1bf6e0358b015e87bf2001381682ad34e3e1034f18504c7522  PlCa.dat
a5143370270e3331e785654e637a567dd2f0c32ff652fc55386edc30c99ef502  PlCl.dat
9d462deb5fff58c8b03e6af2566f53cf7d03598258b69006f2ad041a9c5fb3ee  PlCo.dat
09ab5ba765e4ba06cdc6db0c8751f79ff74770a6cee7bbb06bdd3c5cbdb9050a  PlDk.dat
150e1f8405729afd891731224cd961f243921030f3104078d8a0621975687871  PlDr.dat
22f385438a1d83b908cbbd9e5dc41f5df5cf0ed35d77a1519673cd95f1dd4116  PlFe.dat
eb06d4c6fed4da7db91a52c548b2077b19e2134bf6e452a5ce4047f949a6c89b  PlGn.dat
db8ec1cc20987c8a511ede2bac79183e672bad4df7c2e9a65ec85f5037cbcbdf  PlGw.dat
0db49064b733d288d902c84a662911657cc0caf2d27930a297f441244d947dfe  PlKb.dat
1fa1c26dd131c59f010b536b75c90a23a85a7f3233be912bef3db7ac64b63a6f  PlKbCpCl.dat
588aa0e3fe73c40737c4fa6845f901bc38d7d22d1e72d8aa37a8f939929937ca  PlKbCpLg.dat
bad01e546dd3ac1593edcb92680292daa5d36ba1edfc8e3901d0133e201ff71f  PlKbCpLk.dat
49cf5ed9c7c438cfdbe4e5a7511d6dfc56868cc673ef8544335224500a94269a  PlKbCpNs.dat
e597aa37456403aa717d2b28b3563ae156433505acac0307108b50510d8ba2d4  PlKbCpPc.dat
b4bc13cd8022da05115cd53bafa9df3d085bddeef0222213b6b1b33b6077e3e5  PlKbCpSs.dat
9fbc03ea5d7a8f80a295c133d3d4daec434d9fa14c2c9429beed7dbd80347cb0  PlKp.dat
c7af59c6ebda36d1efb75795122969149b614258c5f94341b6e2988321944236  PlLg.dat
8b96743c8dc814e207291f3eaa632183a5e5879a6fa2cfb2c8b4a6824244f4b5  PlLk.dat
3ecd5dd86f81d19ed187e91bf2a711c3f2f4988f1fb2a6125d18c955f029c1ef  PlMr.dat
0c4b7e49c8d18bfb3f5fd8c463d7c00b70292fa971b1ee94caf99548ba5b45b2  PlMs.dat
6d8355d5ad90cb3e4fdafe2bb3d2590c07c58ffcd8bd93f8862392e61574cdd0  PlMt.dat
c3e76c53b3b1af8fa83640344d356cd40924dbce982d7ac9d6ad0c7c8b03ea80  PlNn.dat
ad521edc5047ffee943fa3cc4248017fbf9ff5db1918a2628e71c13487211942  PlNs.dat
5170bb2aa5263cb238505f2580e141b3cd32209dd420537e5521ee22336ad184  PlPc.dat
aa147e37f61d171450ad6b2af66958f13dc166c60d81c1243762db554e94ab1c  PlPk.dat
83f459c784e7f591667d5bbce1232bbd534175c25f5729f0ff0e1a8a07638a82  PlPp.dat
b75ee373521c4deb97ea8863e4d07e37b589dc7756c9e375e750e2043075ce89  PlSk.dat
1c1d4a63f6790978a5df152e2877690b64107a4c4417ddf7670209a781066407  PlSs.dat
ef9b718118c6ca8d51d1126fedf42caf1d3467aaeb0c1f1efbb3eb1be6601fad  PlYs.dat
335689b1a0d9737e68f64fcb8d11e5145bf6117a7d284892e4d92f1445eef4ab  PlZd.dat
HASHES
echo "SD Remix: $(ls "${DEST}" | wc -l) fighter files in ${DEST}"
