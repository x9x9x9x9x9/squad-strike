#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the zip tools/test_updater.cpp unpacks: a package folder holding a
stored file, a deflated one in a subfolder, an empty folder, an entry that
tries to escape the destination, and resources/edge.bin: deflated so that
inflate's output fills test_updater's 64 KiB buffer exactly as a 64 KiB input
chunk ends, which once failed the Melee HD pack's install as "damaged"."""
import random
import sys
import zipfile
import zlib

EDGE_ZEROS = 65641  # zeros, flushed, then 150000 random bytes: the edge case
CHUNK = 65536  # extract_zip's read and inflate buffer size


def edge_stream(raw):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return (c.compress(raw[:EDGE_ZEROS]) + c.flush(zlib.Z_FULL_FLUSH) +
            c.compress(raw[EDGE_ZEROS:]) + c.flush())


def hits_edge(packed):
    d = zlib.decompressobj(-15)
    out = 0
    for i in range(0, len(packed), CHUNK):
        out += len(d.decompress(packed[i:i + CHUNK]))
        if out % CHUNK == 0 and not d.eof:
            return True
    return False


class EdgeCompressor:
    """Stands in for zipfile's compressor to write edge_stream's bytes."""

    def __init__(self):
        self.raw = b""

    def compress(self, data):
        self.raw += data
        return b""

    def flush(self):
        return edge_stream(self.raw)


rng = random.Random(20261128)
edge = bytes(EDGE_ZEROS) + bytes(rng.getrandbits(8) for _ in range(150000))
if not hits_edge(edge_stream(edge)):
    sys.exit("make_test_zip: edge.bin no longer hits the buffer edge")

with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("pkg/", "")
    z.writestr("pkg/melee.exe", "program", compress_type=zipfile.ZIP_STORED)
    z.writestr("pkg/resources/a.txt", "z" * 5000, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("pkg/empty/", "")
    real = zipfile._get_compressor
    zipfile._get_compressor = lambda *a, **k: EdgeCompressor()
    try:
        z.writestr("pkg/resources/edge.bin", edge, compress_type=zipfile.ZIP_DEFLATED)
    finally:
        zipfile._get_compressor = real
    z.writestr("pkg/../../escape.txt", "no", compress_type=zipfile.ZIP_STORED)
