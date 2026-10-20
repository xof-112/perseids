"""3F4 panel kit for VCV Rack panels (3rdFloor4).

The house style of 3F4 modules (Perseids first) as code: colours, fonts,
component sizes and the spacing between parts and their labels. A module's
panel_def.py builds its panel from these calls and gets the same look:

    import panel_3f4 as p
    pn = p.Panel(hp=22, title="PERSEIDS")
    pn.display(y=13.0)                       # 256 × 64 NT display 1:1
    pn.big_knob(20, 50, "BLEND", sub="PUSH ⇄ DRY/WET", push=True)
    pn.encoder(22, 80, "TRAIL", steps=True, push="SOLO")
    pn.button(44.9, 80, "REC")
    pn.jack(14, 120, "IN L", ring=p.RING_IN_L)
    pn.save("res/MyModule.svg", "test/panel_parts.txt")

All sizes are mm; x/y are centres (as createParamCentered expects); text
sizes are cap heights; text becomes SVG paths (Rack's renderer has no fonts).
See 3F4_PANEL_STYLE.md for the rules behind the numbers.
Needs fontTools; fonts: DejaVu (labels), Liberation Sans (logo).
"""
import os

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

# --- colours ---------------------------------------------------------------------
BG = "#1b1c1f"         # panel
BG2 = "#232428"        # disc behind small push buttons
BEZEL = "#060607"      # display frame
INK = "#8fb8ff"        # labels, title, arrows: light blue
DIM = "#7388aa"        # secondary print (PUSH …, sub-labels)
NAVY = "#000041"       # bottom stripe, from the 3F4 logo
RULE = "#34353a"       # horizontal divider above the jack field
SEP = "#4a4b50"        # vertical separators between jack groups
RING_IN_L, RING_IN_R = "#d4314f", "#e8b52c"
RING_OUT_L, RING_OUT_R = "#8a4fd1", "#e8b52c"
LOGO_LIGHT, LOGO_DARK, LOGO_GREY = "#dcdcdc", "#3b3c41", "#5b5c67"

# --- fonts -------------------------------------------------------------------------
FONT_DIR = os.environ.get("FONT_DIR", "/usr/share/fonts/truetype/dejavu")
FACE_LABEL = "DejaVuSansCondensed-Bold.ttf"
FACE_TITLE = "DejaVuSans-Bold.ttf"
FACE_LOGO = os.environ.get("LOGO_FONT", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf")

# --- text sizes (cap height, mm) -------------------------------------------------------
T_TITLE, T_KNOB, T_BUTTON, T_JACK, T_SUB = 4.0, 2.4, 2.0, 1.7, 1.5
TITLE_SPACING = 0.9

# --- geometry (mm) ----------------------------------------------------------------------
PANEL_H = 128.5
HP_MM = 5.08
TITLE_BASELINE = 8.6
LOGO_H, LOGO_RIGHT = 3.2, 12.5      # logo height, right edge distance from the panel edge
SCREW_INSET = (7.5, 3.0)            # x from left/right edge, y from top
STRIPE_H = 3.0                      # navy stripe along the bottom edge
DISPLAY_W = 86.7                    # 256 px at Rack's 75 dpi → 1:1 at 100 % zoom
BEZEL_PAD, BEZEL_R = 2.0, 1.5
JACK_PITCH, JACK_ROW_PITCH = 14.0, 13.0
JACK_LAST_ROW_Y = PANEL_H - STRIPE_H - 5.5   # 120.0: ring stays clear of the stripe
RING_R = 4.9

# Rack components used (componentlibrary) and their size in mm.
COMPONENTS = {
    "RoundBigBlackKnob": 15.2,     # main pots
    "RoundLargeBlackKnob": 12.2,   # encoders
    "VCVButton": 6.1,              # main buttons
    "TL1105": 5.2,                 # small push / step buttons
    "PJ301MPort": 8.0,             # jacks
    "ScrewBlack": 5.1,
}
# Preview SVG name for each component (Rack res/ComponentLibrary).
_PREVIEW = {"RoundBigBlackKnob": "RoundBigBlackKnob", "RoundLargeBlackKnob": "RoundLargeBlackKnob",
            "VCVButton": "VCVButton_0", "TL1105": "TL1105_0", "PJ301MPort": "PJ301M", "ScrewBlack": "ScrewBlack"}

# Label offsets from the part's centre (mm, + = below).
BIG_KNOB_LABEL, BIG_KNOB_SUB = 12.4, 16.0
BIG_KNOB_PUSH = (9.6, 6.0)          # push button offset, lower right
PUSH_DISC_R = 3.4
ENC_LABEL = 9.6
ENC_STEP_DX, ENC_ARROW_DY = 11.5, -4.6
ENC_PUSH_DY, ENC_PUSH_LABEL = 13.8, 4.8
BUTTON_LABEL, BUTTON_SUB = 7.0, 10.0
BUTTON_PITCH = 11.0
JACK_LABEL = -5.8

_fonts = {}


def _font(name):
    if name not in _fonts:
        _fonts[name] = TTFont(name if os.path.isabs(name) else os.path.join(FONT_DIR, name))
    return _fonts[name]


def text_width(s, size, face=FACE_LABEL, spacing=0.0):
    f = _font(face)
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    cap = getattr(f["OS/2"], "sCapHeight", 0) or f["head"].unitsPerEm * 0.73
    return sum(gs[cmap.get(ord(c), cmap[32])].width for c in s) * size / cap + spacing * max(0, len(s) - 1)


def text(s, x, y, size, fill=INK, anchor="middle", face=FACE_LABEL, spacing=0.0):
    """Text as one SVG path; y = baseline, size = cap height (mm)."""
    f = _font(face)
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


def line(x0, y0, x1, y1, col=RULE, sw=0.3):
    return '<line x1="%.3f" y1="%.3f" x2="%.3f" y2="%.3f" stroke="%s" stroke-width="%.2f"/>' % (x0, y0, x1, y1, col, sw)


def triangle(x, y, left, col=INK, d=1.0):
    pts = [(x + d, y - d), (x - d, y), (x + d, y + d)] if left else [(x - d, y - d), (x + d, y), (x - d, y + d)]
    return '<path fill="%s" d="M%s Z"/>' % (col, " L".join("%.2f %.2f" % p for p in pts))


def logo_3f4(x0, y0, h, ink=LOGO_LIGHT, light=LOGO_LIGHT, dark=LOGO_DARK, grey=LOGO_GREY):
    """3F4 logo for a dark panel: four bars (top light, others dark left / grey
    right) and the wordmark. x0, y0 = top left, h = height (mm).
    Returns (svg elements, width)."""
    k = h / 296.0  # artwork pixels → mm (content 47…343 px high)
    parts = []
    for top, bh, first in ((47, 41, True), (130, 41, False), (212, 41, False), (294, 44, False)):
        y = y0 + (top - 47) * k
        if first:
            parts.append(rect(x0, y, 582 * k, bh * k, light))
        else:
            parts.append(rect(x0, y, 145 * k, bh * k, dark))
            parts.append(rect(x0 + 145 * k, y, 437 * k, bh * k, grey))
    parts.append(text("3F4", x0 + 750 * k, y0 + h, 288 * k, ink, anchor="start", face=FACE_LOGO))
    return parts, 1459 * k


class Panel:
    """One panel: background, navy stripe, title, logo and top screws are
    placed at once; the module adds its parts with the calls below."""

    def __init__(self, hp, title):
        self.hp = hp
        self.W, self.H = hp * HP_MM, PANEL_H
        self.CX = self.W / 2
        self.out = [rect(0, 0, self.W, self.H, BG), rect(0, self.H - STRIPE_H, self.W, STRIPE_H, NAVY)]
        self.parts = []  # (component, x, y) for the preview
        self.out.append(text(title, self.CX, TITLE_BASELINE, T_TITLE, INK, face=FACE_TITLE, spacing=TITLE_SPACING))
        _, lw = logo_3f4(0, 0, LOGO_H)
        self.out += logo_3f4(self.W - LOGO_RIGHT - lw, TITLE_BASELINE - LOGO_H, LOGO_H)[0]
        self.screws = [(SCREW_INSET[0], SCREW_INSET[1]), (self.W - SCREW_INSET[0], SCREW_INSET[1])]
        for x, y in self.screws:
            self.parts.append(("ScrewBlack", x, y))
        self.display_rect = None

    def display(self, y=13.0, w=DISPLAY_W, h=None):
        """NT-style display window (256 × 64 at w = 86.7 mm), centred."""
        h = w / 4 if h is None else h
        x = self.CX - w / 2
        self.out.append(rect(x - BEZEL_PAD, y - BEZEL_PAD, w + 2 * BEZEL_PAD, h + 2 * BEZEL_PAD, BEZEL, r=BEZEL_R))
        self.display_rect = (x, y, w, h)
        return self.display_rect

    def big_knob(self, x, y, label, sub=None, push=False):
        """Main pot, label below; push = small button lower right."""
        self.parts.append(("RoundBigBlackKnob", x, y))
        self.out.append(text(label, x, y + BIG_KNOB_LABEL, T_KNOB))
        if sub:
            self.out.append(text(sub, x, y + BIG_KNOB_SUB, T_SUB, DIM))
        if push:
            px, py = x + BIG_KNOB_PUSH[0], y + BIG_KNOB_PUSH[1]
            self.out.append(circle(px, py, PUSH_DISC_R, BG2))
            self.parts.append(("TL1105", px, py))
            return (px, py)
        return None

    def encoder(self, x, y, label, steps=True, push=None):
        """Endless encoder; steps = ◀ / ▶ buttons beside it; push = label of the push button below.
        Returns the positions {'step': [(x,y),(x,y)], 'push': (x,y)}."""
        self.parts.append(("RoundLargeBlackKnob", x, y))
        self.out.append(text(label, x, y + ENC_LABEL, T_KNOB))
        pos = {}
        if steps:
            pos["step"] = [(x - ENC_STEP_DX, y), (x + ENC_STEP_DX, y)]
            for (sx, sy), left in zip(pos["step"], (True, False)):
                self.parts.append(("TL1105", sx, sy))
                self.out.append(triangle(sx, sy + ENC_ARROW_DY, left))
        if push:
            pos["push"] = (x, y + ENC_PUSH_DY)
            self.parts.append(("TL1105", x, y + ENC_PUSH_DY))
            self.out.append(text("PUSH " + push, x, y + ENC_PUSH_DY + ENC_PUSH_LABEL, T_SUB, DIM))
        return pos

    def button(self, x, y, label, sub=None):
        self.parts.append(("VCVButton", x, y))
        self.out.append(text(label, x, y + BUTTON_LABEL, T_BUTTON))
        if sub:
            self.out.append(text(sub, x, y + BUTTON_SUB, T_SUB, DIM))

    def jack(self, x, y, label, ring=None):
        if ring:
            self.out.append(circle(x, y, RING_R, ring))
        self.parts.append(("PJ301MPort", x, y))
        self.out.append(text(label, x, y + JACK_LABEL, T_JACK))

    def jack_columns(self, n=None, first=14.0):
        """x of the jack grid (14 mm pitch, first column 14 mm from the left)."""
        n = n or int((self.W - 2 * first) / JACK_PITCH) + 1
        return [first + JACK_PITCH * k for k in range(n)]

    def rule(self, y):
        self.out.append(line(6.0, y, self.W - 6.0, y, RULE))

    def separator(self, x, y_center):
        self.out.append(line(x, y_center - 6.5, x, y_center + 4.5, SEP))

    def add(self, *svg):
        self.out += list(svg)

    def save(self, svg_path, parts_path=None):
        svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
               '<svg xmlns="http://www.w3.org/2000/svg" width="%.2fmm" height="%.1fmm" viewBox="0 0 %.2f %.1f">\n'
               % (self.W, self.H, self.W, self.H) + "\n".join(self.out) + "\n</svg>\n")
        os.makedirs(os.path.dirname(os.path.abspath(svg_path)), exist_ok=True)
        open(svg_path, "w").write(svg)
        if parts_path:
            with open(parts_path, "w") as fp:
                if self.display_rect:
                    fp.write("DISPLAY %.3f %.3f %.3f %.3f\n" % self.display_rect)
                for name, x, y in self.parts:
                    fp.write("%s %.3f %.3f\n" % (_PREVIEW[name], x, y))


def save_logo(path, h=10.0):
    """The logo alone on panel colour."""
    lp, lw = logo_3f4(1.0, 1.0, h)
    open(path, "w").write('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
                          'width="%.2fmm" height="%.2fmm" viewBox="0 0 %.2f %.2f">\n%s\n%s\n</svg>\n'
                          % (lw + 2, h + 2, lw + 2, h + 2, rect(0, 0, lw + 2, h + 2, BG), "\n".join(lp)))
