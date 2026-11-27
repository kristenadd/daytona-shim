#!/usr/bin/env python3
"""Makes the launcher logo and app icons from your own art, or the generic set.

    python3 make_branding.py --logo LOGO.png [--icon ICON.png] [--icon-back BACK.png]
                             [--ico-source BADGE.png] --out DIR
    python3 make_branding.py --generic --font TITLE.ttf --out DIR

Writes into DIR:
    logo.png               the launcher's title logo (transparent, drawn on a
                           white band; at most 980 x 440)
    icon-1024.png          the app icon (iPhone, iPad, the Mac app), opaque
    icon-vision-back.png   Apple Vision Pro's icon, back layer (opaque)
    icon-vision-front.png  Apple Vision Pro's icon, front layer (transparent;
                           floats over the back one)
    icon.ico               the Windows launcher's icon (16 to 256 px)

--logo is a logo with a transparent background. --icon is a square picture
for the app icon; without it the icon is the logo over --icon-back (or over
the generic background). --icon-back is the icon's background without the
logo; with it, Vision Pro gets the logo as its own floating layer, otherwise
its back layer is the whole icon. --ico-source is a picture for the Windows
icon (transparency kept; it need not be square); without it, icon-1024.png.

Then point branding.ini at DIR (see tools/branding/README.md). Needs Pillow
(python3 -m pip install pillow).
"""
import argparse
import math
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
except ImportError:
    sys.exit("make_branding.py needs Pillow: python3 -m pip install pillow")

SIZE = 1024
LOGO_MAX = (980, 440)
ICO_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def fit_logo(logo):
    """Trimmed to what it draws and scaled to fit 980 x 440."""
    logo = logo.convert("RGBA")
    box = logo.getchannel("A").point(lambda a: 255 if a > 4 else 0).getbbox()
    if box:
        logo = logo.crop(box)
    s = min(LOGO_MAX[0] / logo.width, LOGO_MAX[1] / logo.height)
    return logo.resize((max(1, round(logo.width * s)), max(1, round(logo.height * s))), Image.LANCZOS)


def square(img):
    """Centre-cropped to a square and scaled to 1024, opaque."""
    img = img.convert("RGBA")
    side = min(img.size)
    left, top = (img.width - side) // 2, (img.height - side) // 2
    img = img.crop((left, top, left + side, top + side)).resize((SIZE, SIZE), Image.LANCZOS)
    flat = Image.new("RGBA", img.size, (255, 255, 255, 255))
    flat.alpha_composite(img)
    return flat.convert("RGB")


def centred(logo, scale, dy=0):
    """The logo on a transparent 1024 square, `scale` of its width, inside the round mask."""
    w = round(SIZE * scale)
    h = round(logo.height * w / logo.width)
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    layer.alpha_composite(logo.resize((w, h), Image.LANCZOS), ((SIZE - w) // 2, (SIZE - h) // 2 + dy))
    return layer


# ---- the generic set: no game art ----

def generic_background():
    """Night blue with three racing stripes (red, orange, yellow) sweeping up."""
    img = Image.new("RGB", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - SIZE * 0.5, y - SIZE * 0.35) / SIZE
            t = min(1.0, d * 1.4)
            px[x, y] = (int(22 - 14 * t), int(40 - 26 * t), int(92 - 58 * t))
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for i, colour in enumerate([(230, 32, 48), (244, 128, 24), (250, 206, 40)]):
        off = i * 70
        pts = []
        for k in range(0, 41):
            x = -60 + k * (SIZE + 120) / 40
            pts.append((x, 1010 - off - 380 * max(0.0, x / SIZE) ** 1.6))
        for k in range(40, -1, -1):
            x = -60 + k * (SIZE + 120) / 40
            pts.append((x, 1010 - off - 380 * max(0.0, x / SIZE) ** 1.6 + 44))
        draw.polygon(pts, fill=colour + (255,))
    img = img.convert("RGBA")
    img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(1.2)))
    return img.convert("RGB")


def checkered_flag(width):
    """A waving chequered flag on a pole, transparent around it."""
    cols, rows = 8, 5
    cell = width / (cols + 1.5)
    h = int(cell * rows + cell * 2.5)
    flag = Image.new("RGBA", (int(width), h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(flag)
    pole_x = cell * 0.6
    draw.rounded_rectangle((pole_x - cell * 0.18, cell * 0.2, pole_x + cell * 0.18, h - 2), radius=cell * 0.18,
                           fill=(150, 156, 168, 255))
    wave = lambda u: math.sin(u * math.pi * 1.6) * cell * 0.55  # noqa: E731
    x0 = pole_x + cell * 0.18
    for r in range(rows):
        for c in range(cols):
            corners = []
            for cu, rv in ((c, r), (c + 1, r), (c + 1, r + 1), (c, r + 1)):
                u = cu / cols
                corners.append((x0 + cu * cell, cell * 0.4 + rv * cell + wave(u)))
            draw.polygon(corners, fill=(20, 20, 24, 255) if (r + c) % 2 == 0 else (248, 248, 248, 255))
    return flag


def generic_logo(font_path):
    """The title as plain lettering under a small flag, for the white band."""
    big = ImageFont.truetype(str(font_path), 190)
    small = ImageFont.truetype(str(font_path), 96)
    canvas = Image.new("RGBA", (1400, 700), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    flag = checkered_flag(260)
    canvas.alpha_composite(flag, ((1400 - flag.width) // 2, 10))
    top = 10 + flag.height + 10
    for text, font, colour, y in (("DAYTONA", big, (16, 16, 20, 255), top),
                                  ("CHAMPIONSHIP USA", small, (30, 120, 220, 255), top + 200)):
        w = draw.textlength(text, font=font)
        draw.text(((1400 - w) / 2, y), text, font=font, fill=colour)
    return fit_logo(canvas)


def ico_picture(img):
    """Fitted into a transparent square, for the .ico."""
    img = img.convert("RGBA")
    side = max(img.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.alpha_composite(img, ((side - img.width) // 2, (side - img.height) // 2))
    return sq.resize((256, 256), Image.LANCZOS)


def write_set(out, logo, icon, back, front, ico=None):
    out.mkdir(parents=True, exist_ok=True)
    logo.save(out / "logo.png")
    icon.save(out / "icon-1024.png")
    back.save(out / "icon-vision-back.png")
    front.save(out / "icon-vision-front.png")
    (ico if ico is not None else icon.convert("RGBA").resize((256, 256), Image.LANCZOS)).save(
        out / "icon.ico", sizes=ICO_SIZES)
    for name in ("logo.png", "icon-1024.png", "icon-vision-back.png", "icon-vision-front.png", "icon.ico"):
        print(out / name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logo", type=Path, help="logo, transparent background")
    ap.add_argument("--icon", type=Path, help="square app icon picture")
    ap.add_argument("--icon-back", type=Path, help="the icon's background without the logo")
    ap.add_argument("--ico-source", type=Path, help="picture for the Windows .ico (default: the icon)")
    ap.add_argument("--generic", action="store_true", help="draw the generic set (no game art)")
    ap.add_argument("--font", type=Path, help="title font for --generic (e.g. third_party/fonts/RussoOne-Regular.ttf)")
    ap.add_argument("--out", type=Path, required=True, help="folder to write into")
    a = ap.parse_args()

    if a.generic:
        if a.font is None:
            ap.error("--generic needs --font")
        bg = generic_background()
        flag = checkered_flag(620)
        icon = bg.convert("RGBA")
        front = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        front.alpha_composite(flag, ((SIZE - flag.width) // 2 + 20, (SIZE - flag.height) // 2 - 70))
        icon.alpha_composite(front)
        write_set(a.out, generic_logo(a.font), icon.convert("RGB"), bg, front)
        return

    if a.logo is None:
        ap.error("give --logo (and optionally --icon, --icon-back), or --generic")
    logo = fit_logo(Image.open(a.logo))
    back = square(Image.open(a.icon_back)) if a.icon_back else None
    if a.icon:
        icon = square(Image.open(a.icon))
    else:
        base = (back or generic_background()).convert("RGBA")
        base.alpha_composite(centred(logo, 0.86, 20))
        icon = base.convert("RGB")
    if back is not None:
        front = centred(logo, 0.86, 20)
    else:
        back = icon
        front = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    write_set(a.out, logo, icon, back, front, ico_picture(Image.open(a.ico_source)) if a.ico_source else None)


if __name__ == "__main__":
    main()
