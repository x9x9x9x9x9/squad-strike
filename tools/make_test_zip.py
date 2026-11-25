#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the zip tools/test_updater.cpp unpacks: a package folder holding a
stored file, a deflated one in a subfolder, an empty folder, and an entry
that tries to escape the destination."""
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("pkg/", "")
    z.writestr("pkg/melee.exe", "program", compress_type=zipfile.ZIP_STORED)
    z.writestr("pkg/resources/a.txt", "z" * 5000, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("pkg/empty/", "")
    z.writestr("pkg/../../escape.txt", "no", compress_type=zipfile.ZIP_STORED)
