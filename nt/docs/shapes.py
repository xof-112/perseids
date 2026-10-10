#!/usr/bin/env python3
"""Draws the LFO waveforms as an inline SVG into the guide, from the plug-in's
own ModShape():  ./test/sim --shapes | python3 docs/shapes.py docs/perseids-nt-anleitung.html
Placed between <!-- shapes:start --> and <!-- shapes:end -->."""
import html
import random
import re
import sys

rows = []
for line in sys.stdin:
    parts = line.rstrip("\n").split("\t")
    rows.append((parts[0], [float(v) for v in parts[1:]]))

# Random steps: drawn as an example (the plug-in rolls new values each cycle).
rnd = random.Random(7)
steps = [rnd.uniform(-1, 1) for _ in range(4)]
rows.append(("Random steps", [steps[min(3, i * 4 // 65)] for i in range(65)]))

cw, ch, pad, cols = 118, 64, 10, 6
n = len(rows)
rws = (n + cols - 1) // cols
W, H = cols * cw, rws * ch
out = ['<svg viewBox="0 0 %d %d" width="100%%" role="img" aria-label="LFO-Kurven" '
       'style="max-width:%dpx;color:var(--ink)" xmlns="http://www.w3.org/2000/svg">' % (W, H, W)]
for k, (name, ys) in enumerate(rows):
    x0 = (k % cols) * cw + pad
    y0 = (k // cols) * ch + 6
    w, h = cw - 2 * pad, ch - 26
    out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="currentColor" stroke-opacity=".18"/>'
               % (x0, y0, w, h))
    out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="currentColor" stroke-opacity=".25" stroke-dasharray="2 3"/>'
               % (x0, y0 + h / 2, x0 + w, y0 + h / 2))
    pts = []
    for i, y in enumerate(ys):
        px = x0 + w * i / (len(ys) - 1)
        py = y0 + h / 2 - y * (h / 2 - 3)
        pts.append("%.1f,%.1f" % (px, py))
    # Steps and jumps: draw vertical edges where the value jumps.
    out.append('<polyline points="%s" fill="none" stroke="currentColor" stroke-width="1.6"/>' % " ".join(pts))
    out.append('<text x="%d" y="%d" font-size="11" fill="currentColor" font-family="var(--body)">%s</text>'
               % (x0, y0 + h + 15, html.escape(name)))
out.append("</svg>")
block = "<!-- shapes:start -->\n  " + "".join(out) + "\n  <!-- shapes:end -->"

path = sys.argv[1]
doc = open(path, encoding="utf-8").read()
if "<!-- shapes:start -->" in doc:
    doc = re.sub(r"<!-- shapes:start -->.*?<!-- shapes:end -->", lambda m: block, doc, flags=re.S)
else:
    anchor = '  <h3 id="mod-ansicht">'
    doc = doc.replace(anchor, '  <p><b>Mod n shape</b>, die Kurve des internen LFO (ab Werk <i>Classic</i>, die Dreieck/Sinus-Mischung des Moduls). Ein Durchlauf von links nach rechts, gestrichelt die Mitte:</p>\n  ' + block + "\n" + anchor, 1)
open(path, "w", encoding="utf-8").write(doc)
print("shapes: %d" % n)
