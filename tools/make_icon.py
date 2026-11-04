"""Generates the BUKHARA CINEMA application icon (PNG + ICO).

Design: dark rounded square, gold arch (Bukhara portal) with a film-strip band.
Run: python tools/make_icon.py
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent / "resources" / "icons"
GOLD = (209, 165, 74, 255)
TEAL = (47, 150, 160, 255)
BG = (34, 34, 34, 255)


def draw(size: int) -> Image.Image:
    s = size * 4  # supersample
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = s // 6
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=r, fill=BG)
    # Portal arch
    m = s * 0.18
    top = s * 0.16
    bottom = s * 0.84
    w = s - 2 * m
    d.rectangle([m, top + w / 2, s - m, bottom], fill=GOLD)
    d.ellipse([m, top, s - m, top + w], fill=GOLD)
    # Inner doorway
    im = s * 0.32
    iw = s - 2 * im
    itop = s * 0.34
    d.rectangle([im, itop + iw / 2, s - im, bottom], fill=BG)
    d.ellipse([im, itop, s - im, itop + iw], fill=BG)
    # Film band across the base
    band_top = s * 0.70
    d.rectangle([m, band_top, s - m, bottom], fill=TEAL)
    hole = s * 0.035
    x = m + hole
    while x + hole < s - m:
        d.rectangle([x, band_top + hole, x + hole, band_top + 2 * hole], fill=BG)
        d.rectangle([x, bottom - 2 * hole, x + hole, bottom - hole], fill=BG)
        x += hole * 2.2
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    big = draw(256)
    big.save(ROOT / "bukhara-cinema.png")
    big.save(ROOT / "bukhara-cinema.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == "__main__":
    main()
