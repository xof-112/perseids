#!/usr/bin/env python3
"""Both panels as PNG with Rack's own SVG renderer (test/preview), the display
(test/screens/00.png from `make -C test render`) and the knob names the module
draws at runtime (same font, cap height and wrapping as SlotLabels).
    python3 test/preview_panels.py <Rack>/res/ComponentLibrary
-> test/panel-preview.png, test/panel-preview-large.png"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "style"))
SC = 3
MM = 75 / 25.4 * SC
lib = sys.argv[1]


def render(svg, parts, out, labels=None):
    ppm = os.path.join(HERE, "panel.ppm")
    disp = subprocess.run([os.path.join(HERE, "preview"), lib, svg, parts, str(SC), ppm],
                          capture_output=True, text=True, check=True).stdout.split()
    im = Image.open(ppm).convert("RGB")
    if disp and disp[0] == "DISPLAY":
        x, y, w, h = map(int, disp[1:5])
        scr = os.path.join(HERE, "screens", "00.png")
        if os.path.exists(scr):
            im.paste(Image.open(scr).convert("RGB").resize((w, h), Image.NEAREST), (x + 1, y))
    if labels:
        d = ImageDraw.Draw(im)
        for txt, x, y in labels:
            d.text((x * MM, y * MM), txt, font=FONT, fill=(0x8f, 0xb8, 0xff), anchor="ms")
    im.save(out)
    os.remove(ppm)


import panel_def as pd  # noqa: E402  (regenerates the SVGs, gives the positions)
import panel_3f4 as p  # noqa: E402

FONT = ImageFont.truetype(os.path.join(ROOT, "res", "fonts", "DejaVuSansCondensed-Bold.ttf"),
                          round(p.T_JACK / p.NVG_CAP * MM))
DEFAULTS = ["Partials", "Waveshape", "Umbra/Aurora", "Ensemble", "Pitch Spectra", "Pitch Swarm",
            "Spread", "Scatter", "Direction", "Reso decay", "Reso pitch", "Hold"]


def width(t):
    return FONT.getlength(t) / MM


labels = []
for name, (x, y) in zip(DEFAULTS, pd.SLOTS):
    t = name.upper()
    yb = y + p.ASSIGN_LABEL
    if width(t) <= pd.SLOT_LABEL_MAX_W:
        labels.append((t, x, yb))
        continue
    cands = [i for i, c in enumerate(t) if c in " /" and 0 < i < len(t) - 1]
    i = min(cands, key=lambda i: abs(i - len(t) // 2))
    labels += [(t[:i + 1] if t[i] == "/" else t[:i], x, yb), (t[i + 1:], x, yb + pd.SLOT_LABEL_LINE)]
render(os.path.join(ROOT, "res", "Perseids.svg"), os.path.join(HERE, "panel_parts.txt"), os.path.join(HERE, "panel-preview.png"))
render(os.path.join(ROOT, "res", "Perseids-Large.svg"), os.path.join(HERE, "panel_parts_large.txt"),
       os.path.join(HERE, "panel-preview-large.png"), labels)
print("previews written")
