# Renders the draw log from ./render into PNGs. Fonts are approximations
# (DejaVu), sizes and positions are the plug-in's own.
import sys
from PIL import Image, ImageDraw, ImageFont
SC = 4
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 8)
tiny = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 6)
cap  = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 26)
def grey(c): v = int(c) * 17; return (v, v, int(v * 0.97))
frames = []; cur = None
for line in open(sys.argv[1], encoding="utf-8"):
    p = line.rstrip("\n").split(" ", 6 if line.startswith("T") else 7)
    if p[0] == "F":
        cur = [line[2:].strip(), Image.new("RGB", (256, 64), (0, 0, 0))]; frames.append(cur); continue
    img = cur[1]; d = ImageDraw.Draw(img); d.fontmode = "1"
    if p[0] == "S":
        sh, x0, y0, x1, y1, c = map(int, p[1:7])
        if sh == 1: d.line([x0, y0, x1, y1], fill=grey(c))
        elif sh == 2: d.rectangle([x0, y0, x1, y1], outline=grey(c))
        elif sh == 3: d.rectangle([x0, y0, x1, y1], fill=grey(c))
    else:
        x, y, c, al, sz = map(int, p[1:6]); s = p[6]
        f = tiny if sz == 0 else font
        w = d.textlength(s, font=f)
        if al == 1: x -= w / 2
        elif al == 2: x -= w
        d.text((x, y), s, font=f, fill=grey(c), anchor="ls")
W = 256 * SC; pad = 24; capH = 44
sheet = Image.new("RGB", (W + 2 * pad, len(frames) * (64 * SC + capH + pad) + pad), (24, 24, 26))
y = pad
for title, img in frames:
    ImageDraw.Draw(sheet).text((pad, y + 30), title, font=cap, fill=(220, 220, 225), anchor="ls")
    sheet.paste(img.resize((W, 64 * SC), Image.NEAREST), (pad, y + capH)); y += 64 * SC + capH + pad
sheet.save(sys.argv[2])
print(len(frames), "screens")
