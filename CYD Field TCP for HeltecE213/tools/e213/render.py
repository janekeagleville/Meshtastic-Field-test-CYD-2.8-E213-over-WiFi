#!/usr/bin/env python3
"""Zamienia zapisy symulatora (shots/*.pbm, 1 bit) na powiekszone PNG - tylko do podgladu ukladu ekranu na hoscie.
Uzycie: render.py [katalog]   (domyslnie shots). Podglad rysowany jest prawdziwymi czcionkami Adafruit_GFX, wiec szerokosci tekstu sa wierne."""
import sys, glob, os
from PIL import Image
S = 4
src = sys.argv[1] if len(sys.argv) > 1 else "shots"
for p in sorted(glob.glob(os.path.join(src, "*.pbm"))):
    im = Image.open(p).convert("L")
    # w PBM 1 = czarny; e-ink: ink czarny na bialym papierze
    im = im.resize((im.width * S, im.height * S), Image.NEAREST)
    bg = Image.new("L", (im.width + 16, im.height + 16), 150)   # szara "ramka" jak obudowa
    bg.paste(im, (8, 8))
    bg.save(p[:-4] + ".png")
print("ok")
