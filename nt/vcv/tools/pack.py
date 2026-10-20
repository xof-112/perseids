#!/usr/bin/env python3
"""dist/<slug> -> dist/<slug>-<version>-<os>-<cpu>.vcvplugin (tar + zstd), for
builds without the zstd tool:  python3 tools/pack.py win-x64"""
import io
import json
import os
import sys
import tarfile
import zstandard

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
man = json.load(open(os.path.join(here, "plugin.json")))
slug, ver = man["slug"], man["version"]
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.GNU_FORMAT) as t:
    t.add(os.path.join(here, "dist", slug), arcname=slug)
out = os.path.join(here, "dist", "%s-%s-%s.vcvplugin" % (slug, ver, sys.argv[1]))
open(out, "wb").write(zstandard.ZstdCompressor(level=19).compress(buf.getvalue()))
print(out)
