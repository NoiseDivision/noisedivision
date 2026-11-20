#!/usr/bin/env python3
"""Makes the social preview images (docs/img/og/<plugin>.jpg, 1200x630) shown when a plugin page
is shared on Discord, Reddit, X, Facebook and so on.   python scripts/site/og.py   (needs Pillow)"""
import pathlib
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from content import PLUGINS

HERE = pathlib.Path(__file__).resolve().parent
DOCS = HERE.parents[1] / "docs"
W, H = 1200, 630


def font(name, size):
    return ImageFont.truetype(str(HERE / "fonts" / name), size)


def hexrgb(h):
    h = h.lstrip("#"); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


for p in PLUGINS:
    acc = hexrgb(p["accent"])
    img = Image.new("RGB", (W, H), (12, 13, 15))
    glow = Image.new("RGB", (W, H), (12, 13, 15))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((560, 260, 1300, 900), fill=tuple(int(c * .35) for c in acc))
    img = Image.blend(img, glow.filter(ImageFilter.GaussianBlur(120)), 1.0)
    shot = Image.open(DOCS / "img" / p["img"]).convert("RGB")
    box_w, box_h = 600, 520
    sc = min(box_w / shot.width, box_h / shot.height)
    shot = shot.resize((int(shot.width * sc), int(shot.height * sc)), Image.LANCZOS)
    x = 1200 - 50 - shot.width + (box_w - shot.width) // 2 if shot.width < box_w else 1200 - 50 - shot.width
    y = (H - shot.height) // 2
    shadow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shadow).rectangle((x + 10, y + 24, x + shot.width + 10, y + shot.height + 24), fill=200)
    img.paste((0, 0, 0), (0, 0), shadow.filter(ImageFilter.GaussianBlur(24)))
    img.paste(shot, (x, y))
    d = ImageDraw.Draw(img)
    d.rectangle((x - 1, y - 1, x + shot.width, y + shot.height), outline=(44, 46, 51))
    left = 64
    d.text((left, 70), "NOISE ", font=font("ChakraPetch-Bold.ttf", 26), fill=(198, 200, 204))
    d.text((left + d.textlength("NOISE ", font=font("ChakraPetch-Bold.ttf", 26)), 70), "DIVISION", font=font("ChakraPetch-Bold.ttf", 26), fill=(255, 48, 56))
    d.text((left, 200), p["kind"].upper(), font=font("ChakraPetch-SemiBold.ttf", 26), fill=acc)
    size = 96
    while d.textlength(p["name"], font=font("ChakraPetch-Bold.ttf", size)) > x - left - 40: size -= 4
    d.text((left, 236), p["name"], font=font("ChakraPetch-Bold.ttf", size), fill=(255, 255, 255))
    d.text((left, 250 + size + 10), "Free VST3 · AU plugin", font=font("ChakraPetch-SemiBold.ttf", 28), fill=(198, 200, 204))
    d.text((left, 250 + size + 50), "Windows & macOS", font=font("ChakraPetch-SemiBold.ttf", 28), fill=(138, 141, 149))
    d.rectangle((left, 520, left + 120, 524), fill=(255, 48, 56))
    d.text((left, 540), "noisedivision.net", font=font("ChakraPetch-SemiBold.ttf", 24), fill=(198, 200, 204))
    out = DOCS / "img" / "og" / f"{p['slug']}.jpg"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, quality=88, optimize=True, progressive=True)
    print("wrote", out.relative_to(HERE.parents[1]))
