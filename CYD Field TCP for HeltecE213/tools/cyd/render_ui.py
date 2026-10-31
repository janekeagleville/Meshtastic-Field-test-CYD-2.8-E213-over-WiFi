#!/usr/bin/env python3
"""Sklada podglady ekranu CYD (320x240) z zapisow test/cyd/ui_shots.cpp: shots_cyd/*.bin (RGB565) + *.txt (napisy).
Grafika (panele, wskazniki, mapa, wykresy) jest 1:1 z kodu rysujacego; napisy sa rysowane czcionka zastepcza
(szerokosci orientacyjne, na ekranie CYD sa czcionki TFT_eSPI). Uzycie: render_ui.py [katalog] [--sheet]"""
import sys, glob, os, struct
from PIL import Image, ImageDraw, ImageFont
S = 3
def rgb(v): return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
def font(path, px):
    try: return ImageFont.truetype(path, px)
    except OSError: return ImageFont.load_default()
F = {1: font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 7 * S),
     2: font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12 * S),
     4: font("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 19 * S)}
H = {1: 8, 2: 16, 4: 26}
def render(base):
    raw = open(base + ".bin", "rb").read()
    px = struct.unpack("<%dH" % (320 * 240), raw)
    im = Image.new("RGB", (320, 240)); im.putdata([rgb(v) for v in px])
    im = im.resize((320 * S, 240 * S), Image.NEAREST)
    d = ImageDraw.Draw(im)
    for line in open(base + ".txt", encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if "|" not in line: continue
        head, s = line.split("|", 1)
        x, y, f, fg, bg = map(int, head.split())
        fnt = F.get(f, F[2]); top = (H.get(f, 16) * S - (fnt.getbbox("Hg")[3])) // 2
        d.text((x * S, y * S + max(0, top)), s, font=fnt, fill=rgb(fg))
    return im
if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "shots_cyd"
    ims = []
    for b in sorted(glob.glob(os.path.join(src, "*.bin"))):
        b = b[:-4]; im = render(b); im.save(b + ".png"); ims.append((os.path.basename(b), im))
    if "--sheet" in sys.argv and ims:
        cols = 2; cw, ch = 320 * S + 30, 240 * S + 50
        sheet = Image.new("RGB", (cols * cw, ((len(ims) + cols - 1) // cols) * ch), (24, 26, 30)); d = ImageDraw.Draw(sheet)
        for i, (n, im) in enumerate(ims):
            x, y = (i % cols) * cw + 15, (i // cols) * ch + 38; sheet.paste(im, (x, y)); d.text((x, y - 30), n, fill=(200, 205, 215), font=F[2])
        sheet.save(os.path.join(src, "sheet.png"))
