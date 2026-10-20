#!/usr/bin/env python3
"""Import library for libRack.dll from the symbols the plug-in objects need,
for a Windows cross-build without the Rack SDK's libRack.dll.a:

    python3 tools/mklibrack.py <RACK_DIR> build/**/*.o
    -> <RACK_DIR>/libRack.dll.a

Only Rack's own symbols (namespace rack, nanovg, jansson, glfw, nanosvg) are
imported; data (vtables, typeinfo, variables) is marked DATA so the linker
auto-imports it instead of pointing at a code thunk."""
import os
import subprocess
import sys

PREFIX = os.environ.get("MINGW_PREFIX", "x86_64-w64-mingw32-")
rack_dir, objs = sys.argv[1], sys.argv[2:]


def nm(args):
    return subprocess.run([PREFIX + "nm"] + args + objs, capture_output=True, text=True).stdout.splitlines()


undef = {l.split()[-1] for l in nm(["-u"]) if l.strip() and not l.endswith(":")}
defined = {l.split()[2] for l in nm(["--defined-only"]) if len(l.split()) == 3}
need = sorted(undef - defined)
rack = [s for s in need if (s.startswith("_Z") and "4rack" in s and not s.startswith(("_ZSt", "_ZNSt", "_ZNKSt")))
        or s.startswith(("nvg", "json_", "glfw", "nsvg"))]
dem = subprocess.run([PREFIX + "c++filt"], input="\n".join(rack), capture_output=True, text=True).stdout.splitlines()
lines = ["LIBRARY libRack.dll", "EXPORTS"]
for s, d in zip(rack, dem):
    data = s.startswith(("_ZTV", "_ZTI", "_ZTS")) or (s.startswith("_Z") and "(" not in d)
    lines.append("    %s%s" % (s, " DATA" if data else ""))
deffile = os.path.join(rack_dir, "libRack.def")
open(deffile, "w").write("\n".join(lines) + "\n")
subprocess.run([PREFIX + "dlltool", "-d", deffile, "-l", os.path.join(rack_dir, "libRack.dll.a"), "-D", "libRack.dll"],
               check=True)
print("libRack.dll.a: %d symbols" % len(rack))
