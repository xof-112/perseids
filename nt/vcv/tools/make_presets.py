#!/usr/bin/env python3
"""Factory presets of the Perseids VCV module (presets/Perseids/*.vcvm, shown
in Rack under right-click → Preset) and test/presets_check.tsv for the host
test (every name must exist, every value fit, every preset must sound).
    cd nt && ./test/sim --list | python3 vcv/tools/make_presets.py
Values are the plug-in's own (as in the NT menu): seconds ×10 for Buffer and
fades, Hz ×100 for LFO rates, mod destinations and enums by name."""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # nt/vcv
targets = {}
for line in sys.stdin:
    f = line.rstrip("\n").split("\t")
    if f[0] == "T":
        targets[f[2]] = int(f[1])

SHAPE = {"Classic": 0, "Sine": 1, "Triangle": 2, "Saw up": 3, "Saw down": 4, "Square": 5, "Shark": 6,
         "Shark rev": 7, "Exp": 8, "Log": 9, "Random steps": 10}
SYNC = {"Free": 0, "/16": 1, "/8": 2, "/4": 3, "/3": 4, "/2": 5, "x1": 6, "x2": 7, "x3": 8, "x4": 9, "x8": 10}
CV = lambda n: n + 2  # panel jack CV n = NT input n + 2


def mod(slot, dest, amount, shape="Classic", sync="Free", rate=25, mode=0, offset=0):
    n = "Mod %d " % slot
    return {n + "dest": targets[dest], n + "amount": amount, n + "offset": offset, n + "shape": SHAPE[shape],
            n + "sync": SYNC[sync], n + "LFO rate": rate, n + "mode": mode, n + "in": 0}


# Rhythmic: a clock on CV 1 (Clock in), its reset on CV 2 (Reset in).
CLOCKED = {"Clock in": CV(1), "Reset in": CV(2), "Clock res": 0, "Beats/bar": 4}

PRESETS = [
    ("Rhythm - Gate Cloud", 10,
     "Square on Blend switches Spectra/Swarm on every beat, random Scan steps on every half beat.",
     dict(CLOCKED, **{"Count": 4, "Cont. Rec": 1, "Threshold": 10, "Buffer": 15, "Hold": 6, "Fade in": 2,
                      "Fade out": 5, "Blend": 50, "Size": 8, "Spread": 30, "Scan": 40, "Scatter": 30,
                      "Partials": 16},
          **mod(1, "Blend", 50, "Square", "x1"), **mod(2, "Scan", 40, "Random steps", "x2"))),
    ("Rhythm - Scan Stutter", 10,
     "Swarm reads the take with a saw-tooth scan over two beats: repeats on the grid. Grain size jumps twice per beat.",
     dict(CLOCKED, **{"Count": 3, "Cont. Rec": 1, "Threshold": 10, "Buffer": 20, "Hold": 10, "Fade in": 1,
                      "Fade out": 3, "Blend": 90, "Size": 6, "Spread": 10, "Scan": 50, "Scatter": 5,
                      "Direction": 0, "Atmosphere": -20},
          **mod(1, "Scan", 100, "Saw up", "/2", mode=1), **mod(2, "Size", 30, "Square", "x2"))),
    ("Rhythm - Pitch Steps", 10,
     "The resonator steps through a pentatonic scale on every beat; Umbra/Aurora breathes over a bar.",
     dict(CLOCKED, **{"Count": 4, "Cont. Rec": 1, "Threshold": 10, "Buffer": 20, "Hold": 8, "Fade in": 3,
                      "Fade out": 10, "Blend": 70, "Size": 12, "Spread": 40, "Scan": 30, "Reso mix": 45,
                      "Reso decay": 70, "Reso quantize": 1, "Reso scale": 2},
          **mod(1, "Reso pitch", 60, "Random steps", "x1"), **mod(2, "Umbra/Aurora", 60, "Triangle", "/4"))),
    ("Rhythm - Bar Swell", 10,
     "Takes only from Rec trig in (patch a bar clock to CV 3); Atmosphere swells once per bar, Dry/Wet pumps on the beat.",
     dict(CLOCKED, **{"Rec trig in": CV(3), "Count": 5, "Cont. Rec": 0, "Threshold": 100, "Overwrite": 1,
                      "Buffer": 20, "Hold": 12, "Fade in": 10, "Fade out": 20, "Blend": 40, "Rec style": 2},
          **mod(1, "Atmosphere", 70, "Shark", "/4"), **mod(2, "Dry/Wet", 25, "Exp", "x1"),
          **mod(3, "Spread", 40, "Sine", "/8"))),
    # Ambient: no clock needed, slow free LFOs.
    ("Ambient - Long Exposure", 20,
     "Long takes, slow fades, wide blurred Swarm; Blend and Umbra/Aurora drift on their own.",
     dict(**{"Count": 5, "Cont. Rec": 1, "Threshold": 8, "Buffer": 80, "Hold": 25, "Fade in": 40,
             "Fade out": 50, "Blend": 35, "Partials": 24, "Ensemble": 60, "Umbra/Aurora": 40, "Size": 22,
             "Spread": 70, "Scatter": 80, "Atmosphere": -50, "Reso mix": 15},
          **mod(1, "Blend", 25, "Sine", rate=5), **mod(2, "Umbra/Aurora", 30, "Triangle", rate=3))),
    ("Ambient - Frozen Choir", 10,
     "Press REC: three takes held forever, Spectra an octave up with a wide ensemble over a major resonator.",
     dict(**{"Count": 3, "Cont. Rec": 0, "Hold": 31, "Buffer": 50, "Fade in": 30, "Fade out": 50, "Blend": 10,
             "Partials": 32, "Waveshape": -15, "Ensemble": 85, "Umbra/Aurora": -30, "Pitch Spectra": 12,
             "Reso mix": 30, "Reso decay": 85, "Reso quantize": 1, "Reso scale": 0},
          **mod(1, "Ensemble", 20, "Sine", rate=4), **mod(2, "Waveshape", 25, "Log", rate=2))),
    ("Ambient - Dust Drift", 10,
     "Swarm only, an octave down, random direction and radiation; the scan head wanders in slow random steps.",
     dict(**{"Count": 4, "Cont. Rec": 1, "Threshold": 10, "Buffer": 60, "Hold": 20, "Fade in": 30,
             "Fade out": 40, "Blend": 100, "Size": 24, "Spread": 85, "Scatter": 95, "Direction": 2,
             "Atmosphere": 60, "Pitch Swarm": -12, "Dry/Wet": 90},
          **mod(1, "Scan", 40, "Random steps", rate=10), **mod(2, "Spread", 25, "Triangle", rate=3))),
    ("Ambient - Resonant Shore", 10,
     "The resonator in front: pentatonic, just tuning, its pitch drifting slowly; the cloud washes underneath.",
     dict(**{"Count": 4, "Cont. Rec": 1, "Threshold": 10, "Buffer": 40, "Hold": 20, "Fade in": 30,
             "Fade out": 40, "Blend": 60, "Size": 18, "Spread": 50, "Atmosphere": -30, "Reso mix": 70,
             "Reso decay": 90, "Reso damping": 30, "Reso spread": 65, "Reso quantize": 1, "Reso scale": 2,
             "Reso tuning": 1, "Dry/Wet": 85},
          **mod(1, "Reso pitch", 30, "Log", rate=2), **mod(2, "Reso decay", 15, "Sine", rate=4))),
]

DEFAULTS = {"Blend": 50, "Scan": 20, "Size": 16}
out_dir = os.path.join(ROOT, "presets", "Perseids")
os.makedirs(out_dir, exist_ok=True)
for f in os.listdir(out_dir):
    if f.endswith(".vcvm"):
        os.remove(os.path.join(out_dir, f))
version = json.load(open(os.path.join(ROOT, "plugin.json")))["version"]
check = []
for name, seconds, note, values in PRESETS:
    v = lambda k: values.get(k, DEFAULTS[k])
    # Pots where their targets are, so they need no catch-up after loading.
    params = [{"value": v("Blend") / 100.0, "id": 0}, {"value": v("Scan") / 100.0, "id": 1},
              {"value": (v("Size") - 4) / 20.0, "id": 2}]
    preset = {"plugin": "3F4-Perseids", "model": "Perseids", "version": version, "params": params,
              "data": {"specifications": [seconds], "values": values, "note": note}}
    with open(os.path.join(out_dir, name + ".vcvm"), "w") as fp:
        json.dump(preset, fp, indent=1)
    for k, val in values.items():
        check.append("%s\t%d\t%s\t%d" % (name, seconds, k, val))
open(os.path.join(ROOT, "test", "presets_check.tsv"), "w").write("\n".join(check) + "\n")
print("%d presets" % len(PRESETS))
