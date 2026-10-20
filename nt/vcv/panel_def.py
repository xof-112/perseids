#!/usr/bin/env python3
"""Perseids VCV panel: the one place where positions are defined.

Writes res/Perseids.svg (text as paths, Rack's SVG renderer has no fonts) and
src/panel_layout.hpp (the same positions for the module widget).
    python3 panel_def.py
Needs fontTools and the DejaVu fonts (FONT_DIR to override).
"""
import os
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.environ.get("FONT_DIR", "/usr/share/fonts/truetype/dejavu")

HP = 22
W, H = HP * 5.08, 128.5
CX = W / 2

# Dark panel, amber print, coloured rings for audio in/out.
BG, BG2, AMBER, DIM, BEZEL = "#1b1c1f", "#232428", "#e3a43f", "#8d8577", "#060607"
RING_IN, RING_OUT = ("#d4314f", "#e8b52c"), ("#8a4fd1", "#e8b52c")

# --- positions (mm, centres) ---------------------------------------------------
# Display: 256 × 64 NT pixels, 86.7 mm wide = exactly 1 pixel per screen pixel at 100 % zoom.
DISPLAY = (CX - 86.7 / 2, 13.0, 86.7, 86.7 / 4)  # x, y, w, h
POT_Y, POT_X = 50.0, (20.0, CX, W - 20.0)
POT_PUSH = (9.6, 6.0)  # offset of each pot's push button
ENC_Y, ENC_X = 80.0, (22.0, W - 22.0)
ENC_STEP_DX = 11.5     # ◀ / ▶ buttons left and right of each encoder
ENC_PUSH_Y = 93.8
CENTER_BTN_Y = 80.0
CENTER_BTN_X = (CX - 11.0, CX, CX + 11.0)  # REC (button 3), HOLD INF (button 4), MENU
SCREWS = [(7.5, 3.0), (W - 7.5, 3.0), (70.0, H - 3.0)]  # bottom one in the free jack column
JACK_X = [14.0 + 14.0 * k for k in range(7)]
JACK_Y = (109.0, 121.5)
# Inputs 0…10 = NT busses 1…11: In L, In R, CV 3…11. Outputs 0/1 = busses 13/14.
INPUTS = [("IN L", 0, 1), ("IN R", 1, 1)] + [("CV %d" % (3 + k), k, 0) for k in range(7)] + \
         [("CV 10", 2, 1), ("CV 11", 3, 1)]
OUTPUTS = [("OUT L", 5, 1), ("OUT R", 6, 1)]

fonts = {}


def font(name):
    if name not in fonts:
        fonts[name] = TTFont(os.path.join(FONT_DIR, name))
    return fonts[name]


def text(s, x, y, size, fill=AMBER, anchor="middle", face="DejaVuSansCondensed-Bold.ttf", spacing=0.0):
    """Text as one SVG path; y is the baseline, size the cap height in mm."""
    f = font(face)
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    cap = getattr(f["OS/2"], "sCapHeight", 0) or f["head"].unitsPerEm * 0.73
    k = size / cap
    glyphs = [gs[cmap.get(ord(ch), cmap[32])] for ch in s]
    width = sum(g.width for g in glyphs) * k + spacing * max(0, len(s) - 1)
    x0 = x - width / 2 if anchor == "middle" else (x - width if anchor == "end" else x)
    pen = SVGPathPen(gs)
    for g in glyphs:
        g.draw(TransformPen(pen, (k, 0, 0, -k, x0, y)))
        x0 += g.width * k + spacing
    return '<path fill="%s" d="%s"/>' % (fill, pen.getCommands())


def rect(x, y, w, h, fill, r=0.0):
    return '<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="%.2f" fill="%s"/>' % (x, y, w, h, r, fill)


def circle(x, y, r, fill):
    return '<circle cx="%.3f" cy="%.3f" r="%.3f" fill="%s"/>' % (x, y, r, fill)


def line(x0, y0, x1, y1, col=DIM, sw=0.3):
    return '<line x1="%.3f" y1="%.3f" x2="%.3f" y2="%.3f" stroke="%s" stroke-width="%.2f"/>' % (x0, y0, x1, y1, col, sw)


def triangle(x, y, left, col=AMBER):
    d = 1.0
    pts = [(x + d, y - d), (x - d, y), (x + d, y + d)] if left else [(x - d, y - d), (x + d, y), (x - d, y + d)]
    return '<path fill="%s" d="M%s Z"/>' % (col, " L".join("%.2f %.2f" % p for p in pts))


out = [rect(0, 0, W, H, BG)]
out.append(text("PERSEIDS", CX, 8.6, 4.0, AMBER, face="DejaVuSans-Bold.ttf", spacing=0.9))
dx, dy, dw, dh = DISPLAY
out.append(rect(dx - 2.0, dy - 2.0, dw + 4.0, dh + 4.0, BEZEL, r=1.5))

# Pots: default target, and what a push switches to.
for x, name, alt in zip(POT_X, ("BLEND", "SCAN", "SIZE"), ("DRY/WET", "RESO MIX", "ATMOS")):
    out.append(text(name, x, POT_Y + 12.4, 2.4))
    out.append(text("PUSH ⇄ " + alt, x, POT_Y + 16.0, 1.5, DIM))
    out.append(circle(x + POT_PUSH[0], POT_Y + POT_PUSH[1], 3.4, BG2))

# Encoders with ◀ ▶ and push.
for x, name, push in zip(ENC_X, ("TRAIL", "LEVEL"), ("SOLO", "LOCK")):
    out.append(text(name, x, ENC_Y + 9.6, 2.4))
    out.append(triangle(x - ENC_STEP_DX, ENC_Y - 4.6, True))
    out.append(triangle(x + ENC_STEP_DX, ENC_Y - 4.6, False))
    out.append(text("PUSH " + push, x, ENC_PUSH_Y + 4.8, 1.5, DIM))
for x, name in zip(CENTER_BTN_X, ("REC", "HOLD", "MENU")):
    out.append(text(name, x, CENTER_BTN_Y + 7.0, 2.0))
out.append(text("INF", CENTER_BTN_X[1], CENTER_BTN_Y + 10.0, 1.5, DIM))

# Jacks
out.append(line(6.0, 100.4, W - 6.0, 100.4, "#34353a", 0.3))
for xs in ((JACK_X[1] + JACK_X[2]) / 2, (JACK_X[4] + JACK_X[5]) / 2):
    out.append(line(xs, JACK_Y[1] - 6.5, xs, JACK_Y[1] + 4.5, "#4a4b50", 0.3))
for (label, col, row), ring in zip(INPUTS[:2], RING_IN):
    out.append(circle(JACK_X[col], JACK_Y[row], 4.9, ring))
for (label, col, row), ring in zip(OUTPUTS, RING_OUT):
    out.append(circle(JACK_X[col], JACK_Y[row], 4.9, ring))
for label, col, row in INPUTS + OUTPUTS:
    out.append(text(label, JACK_X[col], JACK_Y[row] - 6.2, 1.7))

svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
       '<svg xmlns="http://www.w3.org/2000/svg" width="%.2fmm" height="%.1fmm" viewBox="0 0 %.2f %.1f">\n'
       % (W, H, W, H) + "\n".join(out) + "\n</svg>\n")
os.makedirs(os.path.join(HERE, "res"), exist_ok=True)
open(os.path.join(HERE, "res", "Perseids.svg"), "w").write(svg)


def arr(xs):
    return ", ".join("%.3ff" % x for x in xs)


hpp = """// Generated by panel_def.py — do not edit by hand.
#pragma once
#include <rack.hpp>

namespace panel_layout
{
using namespace rack;
constexpr int kHp = %d;
inline Vec DisplayPos() { return mm2px(Vec(%.3ff, %.3ff)); }
inline Vec DisplaySize() { return mm2px(Vec(%.3ff, %.3ff)); }
inline Vec Pot(int k) { const float x[3] = {%s}; return mm2px(Vec(x[k], %.3ff)); }
inline Vec PotPush(int k) { const float x[3] = {%s}; return mm2px(Vec(x[k] + %.3ff, %.3ff)); }
inline Vec Enc(int k) { const float x[2] = {%s}; return mm2px(Vec(x[k], %.3ff)); }
inline Vec EncStep(int k, int dir) { const float x[2] = {%s}; return mm2px(Vec(x[k] + dir * %.3ff, %.3ff)); }
inline Vec EncPush(int k) { const float x[2] = {%s}; return mm2px(Vec(x[k], %.3ff)); }
// 0 = REC (NT button 3), 1 = HOLD INF (button 4), 2 = MENU
inline Vec CenterButton(int k) { const float x[3] = {%s}; return mm2px(Vec(x[k], %.3ff)); }
// Inputs 0…10 = NT busses 1…11 (In L, In R, CV 3…11); outputs 0/1 = busses 13/14.
inline Vec InputPos(int k)
{
    const float x[11] = {%s};
    const float y[11] = {%s};
    return mm2px(Vec(x[k], y[k]));
}
inline Vec Screw(int k) { const float x[3] = {%s}; const float y[3] = {%s}; return mm2px(Vec(x[k], y[k])); }
constexpr int kNumScrews = 3;
inline Vec OutputPos(int k) { const float x[2] = {%s}; return mm2px(Vec(x[k], %.3ff)); }
} // namespace panel_layout
""" % (HP, DISPLAY[0], DISPLAY[1], DISPLAY[2], DISPLAY[3], arr(POT_X), POT_Y, arr(POT_X), POT_PUSH[0],
       POT_Y + POT_PUSH[1], arr(ENC_X), ENC_Y, arr(ENC_X), ENC_STEP_DX, ENC_Y, arr(ENC_X), ENC_PUSH_Y,
       arr(CENTER_BTN_X), CENTER_BTN_Y, arr(JACK_X[c] for _, c, _ in INPUTS), arr(JACK_Y[r] for _, _, r in INPUTS),
       arr(x for x, _ in SCREWS), arr(y for _, y in SCREWS), arr(JACK_X[c] for _, c, _ in OUTPUTS), JACK_Y[1])
open(os.path.join(HERE, "src", "panel_layout.hpp"), "w").write(hpp)

# Parts list for the preview (test/preview): Rack component SVG and centre in mm.
parts = [("RoundBigBlackKnob", x, POT_Y) for x in POT_X]
parts += [("TL1105_0", x + POT_PUSH[0], POT_Y + POT_PUSH[1]) for x in POT_X]
parts += [("RoundLargeBlackKnob", x, ENC_Y) for x in ENC_X]
parts += [("TL1105_0", x + d * ENC_STEP_DX, ENC_Y) for x in ENC_X for d in (-1, 1)]
parts += [("TL1105_0", x, ENC_PUSH_Y) for x in ENC_X]
parts += [("VCVButton_0", x, CENTER_BTN_Y) for x in CENTER_BTN_X]
parts += [("PJ301M", JACK_X[c], JACK_Y[r]) for _, c, r in INPUTS + OUTPUTS]
parts += [("ScrewBlack", x, y) for x, y in SCREWS]
with open(os.path.join(HERE, "test", "panel_parts.txt"), "w") as fp:
    fp.write("DISPLAY %.3f %.3f %.3f %.3f\n" % DISPLAY)
    for name, x, y in parts:
        fp.write("%s %.3f %.3f\n" % (name, x, y))
print("panel: %d HP, %d inputs, %d outputs" % (HP, len(INPUTS), len(OUTPUTS)))
