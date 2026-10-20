# Turns the renderer's output into PNGs. Each frame carries NT_screen as
# drawn with nt_emu's own fonts and line code (render/nt_raster.h), so the
# pictures are pixel for pixel what nt_emu shows: 16 grey levels, white on black.
#   python3 render.py frames.txt perseids-screens.png [screens-dir]
import os
import sys
from PIL import Image, ImageDraw, ImageFont

SC = 4
cap = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 26)
frames, cur = [], None
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("F "):
        cur = [line[2:].strip(), []]
        frames.append(cur)
    elif line.startswith("P ") and cur:
        cur[1].append(line[2:].strip())


def screen(rows):
    img = Image.new("L", (256, 64), 0)
    img.putdata([int(ch, 16) * 17 for r in rows for ch in r])
    return img.convert("RGB")


imgs = [(t, screen(r)) for t, r in frames if len(r) == 64]
if len(sys.argv) > 3:
    os.makedirs(sys.argv[3], exist_ok=True)
    for i, (t, img) in enumerate(imgs):
        img.resize((256 * SC, 64 * SC), Image.NEAREST).save(os.path.join(sys.argv[3], "%02d.png" % i), optimize=True)
W, pad, capH = 256 * SC, 24, 44
sheet = Image.new("RGB", (W + 2 * pad, len(imgs) * (64 * SC + capH + pad) + pad), (24, 24, 26))
y = pad
for title, img in imgs:
    ImageDraw.Draw(sheet).text((pad, y + 30), title, font=cap, fill=(220, 220, 225), anchor="ls")
    sheet.paste(img.resize((W, 64 * SC), Image.NEAREST), (pad, y + capH))
    y += 64 * SC + capH + pad
sheet.save(sys.argv[2])
print(len(imgs), "screens")
