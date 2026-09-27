"""Generate the MillerScore itch.io marketing image set with Pillow.

The images are deliberately illustrated feature panels, not screenshots.  This
keeps the launch material reproducible and avoids presenting unvalidated UI as
literal product capture.
"""

from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageFilter


ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "marketing" / "itchio" / "assets"
LOGO = ROOT / "share" / "icons" / "millerscore_logo_full.png"

INK = "#0A0908"
PANEL = "#17140F"
PANEL_2 = "#211C13"
GOLD = "#D2A43A"
GOLD_LIGHT = "#F2D477"
IVORY = "#F5EEDF"
MUTED = "#B9AB93"
GREEN = "#63C995"
RED = "#E56A67"
BLUE = "#6BA6D9"


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    names = ["seguisb.ttf", "segoeuib.ttf"] if bold else ["segoeui.ttf"]
    names += ["arialbd.ttf" if bold else "arial.ttf"]
    for name in names:
        candidate = Path("C:/Windows/Fonts") / name
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default(size=size)


def gradient(size: tuple[int, int], top: str = "#1C1509", bottom: str = INK) -> Image.Image:
    w, h = size
    a = tuple(int(top[i : i + 2], 16) for i in (1, 3, 5))
    b = tuple(int(bottom[i : i + 2], 16) for i in (1, 3, 5))
    im = Image.new("RGB", size)
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(a[i] * (1 - t) + b[i] * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return im


def add_glow(im: Image.Image, center: tuple[int, int], radius: int, color=(210, 164, 58), alpha=85) -> None:
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    glow = Image.new("L", (radius * 2, radius * 2), 0)
    gd = ImageDraw.Draw(glow)
    gd.ellipse((0, 0, radius * 2 - 1, radius * 2 - 1), fill=alpha)
    glow = glow.filter(ImageFilter.GaussianBlur(radius // 3))
    tint = Image.new("RGBA", glow.size, (*color, 0))
    tint.putalpha(glow)
    layer.alpha_composite(tint, (center[0] - radius, center[1] - radius))
    im.alpha_composite(layer)


def logo(im: Image.Image, box: tuple[int, int, int, int]) -> None:
    src = Image.open(LOGO).convert("RGBA")
    x0, y0, x1, y1 = box
    src.thumbnail((x1 - x0, y1 - y0), Image.Resampling.LANCZOS)
    im.alpha_composite(src, (x0, y0 + (y1 - y0 - src.height) // 2))


def rr(draw: ImageDraw.ImageDraw, xy, radius=20, fill=PANEL, outline=None, width=1):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def label(draw: ImageDraw.ImageDraw, xy, text, *, fill=IVORY, size=28, bold=False, anchor=None):
    draw.text(xy, text, font=font(size, bold), fill=fill, anchor=anchor)


def pill(draw: ImageDraw.ImageDraw, x: int, y: int, text: str, color=GOLD) -> int:
    f = font(20, True)
    bb = draw.textbbox((0, 0), text, font=f)
    w = bb[2] - bb[0] + 30
    rr(draw, (x, y, x + w, y + 36), 18, "#2C2415", color, 1)
    draw.text((x + 15, y + 7), text, font=f, fill=color)
    return w


def staff(draw: ImageDraw.ImageDraw, box, note_color=GOLD_LIGHT):
    x0, y0, x1, y1 = box
    gap = (y1 - y0) / 6
    for i in range(5):
        y = y0 + gap * (i + 1)
        draw.line((x0, y, x1, y), fill="#65563B", width=2)
    for i, (t, p) in enumerate([(0.08, 3.6), (0.22, 2.1), (0.36, 4.2), (0.51, 1.6), (0.68, 3.1), (0.84, 2.4)]):
        x = x0 + (x1 - x0) * t
        y = y0 + gap * p
        draw.ellipse((x - 8, y - 6, x + 8, y + 6), fill=note_color)
        draw.line((x + 7, y, x + 7, y - 38), fill=note_color, width=3)


def piano_roll(draw: ImageDraw.ImageDraw, box):
    x0, y0, x1, y1 = box
    draw.rectangle(box, fill="#10100F", outline="#4D402A")
    rows = 10
    cols = 16
    for r in range(rows + 1):
        y = y0 + (y1 - y0) * r / rows
        draw.line((x0, y, x1, y), fill="#29251E", width=1)
    for c in range(cols + 1):
        x = x0 + (x1 - x0) * c / cols
        draw.line((x, y0, x, y1), fill="#4A3B22" if c % 4 == 0 else "#28241C", width=2 if c % 4 == 0 else 1)
    notes = [(1, 6, 3), (4, 5, 2), (6, 4, 4), (10, 7, 2), (12, 3, 3)]
    cw = (x1 - x0) / cols
    rh = (y1 - y0) / rows
    for c, r, n in notes:
        rr(draw, (x0 + c * cw + 3, y0 + r * rh + 3, x0 + (c + n) * cw - 3, y0 + (r + 1) * rh - 3), 7, GOLD, GOLD_LIGHT)


def waveform(draw: ImageDraw.ImageDraw, box, color=GOLD_LIGHT, seed=12):
    x0, y0, x1, y1 = box
    random.seed(seed)
    mid = (y0 + y1) / 2
    points_top = []
    points_bottom = []
    for x in range(x0, x1 + 1, 5):
        env = 0.2 + 0.8 * math.sin((x - x0) / max(1, x1 - x0) * math.pi) ** 0.7
        amp = (0.15 + random.random() * 0.85) * (y1 - y0) * 0.42 * env
        points_top.append((x, mid - amp))
        points_bottom.append((x, mid + amp))
    draw.polygon(points_top + list(reversed(points_bottom)), fill=color)


def window(draw: ImageDraw.ImageDraw, box, title="MillerScore"):
    x0, y0, x1, y1 = box
    rr(draw, box, 24, "#11100E", "#66522A", 2)
    draw.rounded_rectangle((x0, y0, x1, y0 + 50), radius=24, fill="#211D16")
    draw.rectangle((x0, y0 + 25, x1, y0 + 50), fill="#211D16")
    for i, c in enumerate((RED, GOLD_LIGHT, GREEN)):
        draw.ellipse((x0 + 20 + i * 25, y0 + 18, x0 + 32 + i * 25, y0 + 30), fill=c)
    label(draw, (x0 + 120, y0 + 14), title, fill=MUTED, size=18)


def save(im: Image.Image, name: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    im.convert("RGB").save(OUT / name, "PNG", optimize=True)


def cover():
    im = gradient((630, 500)).convert("RGBA")
    add_glow(im, (475, 130), 240)
    d = ImageDraw.Draw(im)
    for y in (300, 330, 360, 390, 420):
        d.line((45, y, 585, y), fill="#3D321F", width=2)
    logo(im, (52, 40, 578, 185))
    label(d, (315, 224), "NOTATION + PRODUCTION", size=25, bold=True, fill=GOLD_LIGHT, anchor="mm")
    piano_roll(d, (55, 275, 575, 438))
    d.line((355, 275, 355, 438), fill=RED, width=3)
    rr(d, (153, 455, 477, 489), 17, "#291F10", GOLD, 1)
    label(d, (315, 472), "EARLY ACCESS • WINDOWS", size=17, bold=True, fill=GOLD_LIGHT, anchor="mm")
    save(im, "cover-630x500.png")


def banner():
    im = gradient((960, 300)).convert("RGBA")
    add_glow(im, (720, 100), 300)
    d = ImageDraw.Draw(im)
    logo(im, (48, 48, 710, 205))
    label(d, (62, 232), "Write the score. Shape the performance. Produce the sound.", size=25, fill=IVORY)
    for y in (74, 100, 126, 152, 178):
        d.line((720, y, 920, y), fill="#4A3B22", width=2)
    staff(d, (720, 55, 920, 205))
    save(im, "banner-960x300.png")


def feature_unified():
    im = gradient((1600, 900)).convert("RGBA")
    d = ImageDraw.Draw(im)
    pill(d, 80, 62, "01 • CONNECTED WORKFLOW")
    label(d, (80, 125), "From score to piano roll", size=58, bold=True)
    label(d, (82, 198), "The same music, two ways to edit.", size=28, fill=MUTED)
    window(d, (80, 285, 1520, 820), "MillerScore — Score + DAW")
    staff(d, (145, 360, 1455, 525))
    d.line((115, 557, 1485, 557), fill="#5A4827", width=2)
    piano_roll(d, (145, 600, 1455, 770))
    d.line((945, 335, 945, 780), fill=RED, width=4)
    label(d, (80, 858), "Selection, playback, and editing stay connected to the score.", size=23, fill=GOLD_LIGHT)
    save(im, "feature-01-score-daw-1600x900.png")


def feature_audio():
    im = gradient((1600, 900), "#17100C", INK).convert("RGBA")
    add_glow(im, (1340, 250), 330, (229, 106, 103), 50)
    d = ImageDraw.Draw(im)
    pill(d, 80, 62, "02 • AUDIO")
    label(d, (80, 125), "Record and organize your takes", size=58, bold=True)
    label(d, (82, 198), "Import WAV, MP3, and FLAC. Record ASIO inputs to WAV.", size=28, fill=MUTED)
    window(d, (80, 285, 1520, 820), "MillerScore — Audio tracks")
    for i, name in enumerate(("Lead Vocal", "Guitar DI", "Reference")):
        y = 360 + i * 130
        rr(d, (135, y, 340, y + 98), 12, PANEL_2, "#514229")
        label(d, (160, y + 18), name, size=23, bold=True)
        label(d, (160, y + 57), "M   R", size=19, fill=RED if i == 1 else MUTED, bold=True)
        rr(d, (365, y, 1450, y + 98), 12, "#12110F", "#514229")
        waveform(d, (395, y + 18, 1415, y + 80), (GOLD_LIGHT if i != 1 else RED), seed=20 + i)
    rr(d, (1300, 305, 1450, 345), 20, "#351717", RED, 2)
    label(d, (1375, 325), "● REC  00:18", size=18, bold=True, fill="#FFD5D3", anchor="mm")
    label(d, (80, 858), "Early Access: ASIO recording; input selection and latency compensation are evolving.", size=21, fill=GOLD_LIGHT)
    save(im, "feature-02-audio-1600x900.png")


def feature_instruments():
    im = gradient((1600, 900), "#111711", INK).convert("RGBA")
    add_glow(im, (1250, 220), 360, (99, 201, 149), 50)
    d = ImageDraw.Draw(im)
    pill(d, 80, 62, "03 • INSTRUMENTS")
    label(d, (80, 125), "Choose the sound that fits the idea", size=54, bold=True)
    label(d, (82, 198), "SoundFonts, VST3, and compatible Muse ecosystem resources.", size=28, fill=MUTED)
    cards = [
        ("SF2", "SoundFonts", "Lightweight, portable banks", GOLD_LIGHT),
        ("VST3", "VST3 instruments", "Native plug-ins and editors", GREEN),
        ("M", "Muse Sounds", "When installed on the system", BLUE),
    ]
    for i, (mark, title, body, color) in enumerate(cards):
        x = 80 + i * 500
        rr(d, (x, 305, x + 440, 720), 28, PANEL, "#5A4A2E", 2)
        rr(d, (x + 38, 345, x + 160, 467), 28, "#272015", color, 2)
        label(d, (x + 99, 406), mark, size=31, bold=True, fill=color, anchor="mm")
        label(d, (x + 38, 515), title, size=31, bold=True)
        label(d, (x + 38, 565), body, size=21, fill=MUTED)
        label(d, (x + 38, 650), "Browse • assign • save", size=18, fill=color)
    label(d, (80, 820), "Plug-ins, libraries, and Muse Sounds are installed and licensed separately.", size=22, fill=GOLD_LIGHT)
    save(im, "feature-03-instruments-1600x900.png")


def feature_midi():
    im = gradient((1600, 900), "#11131A", INK).convert("RGBA")
    add_glow(im, (1240, 230), 330, (107, 166, 217), 55)
    d = ImageDraw.Draw(im)
    pill(d, 80, 62, "04 • PERFORMANCE MIDI", BLUE)
    label(d, (80, 125), "Performance details that breathe", size=54, bold=True)
    label(d, (82, 198), "Velocity, timing, and duration without rewriting the score.", size=28, fill=MUTED)
    window(d, (80, 285, 1520, 820), "MillerScore — Control lanes")
    piano_roll(d, (145, 350, 1455, 575))
    d.line((145, 615, 1455, 615), fill="#66522A", width=2)
    for i, height in enumerate((44, 92, 61, 122, 78, 140, 105, 70, 128, 56, 96, 116)):
        x = 190 + i * 100
        d.line((x, 765, x, 765 - height), fill=GOLD_LIGHT if i % 3 else BLUE, width=12)
        d.ellipse((x - 8, 757 - height, x + 8, 773 - height), fill=IVORY)
    label(d, (140, 790), "NOTE VELOCITY", size=18, bold=True, fill=GOLD_LIGHT)
    label(d, (80, 858), "CC 0–127: editing and persistence available; generic playback is still in development.", size=21, fill=GOLD_LIGHT)
    save(im, "feature-04-midi-1600x900.png")


def social():
    im = gradient((1200, 630)).convert("RGBA")
    add_glow(im, (900, 150), 380)
    d = ImageDraw.Draw(im)
    logo(im, (70, 55, 850, 240))
    label(d, (78, 290), "NOTATION AND PRODUCTION", size=44, bold=True, fill=IVORY)
    label(d, (78, 348), "IN ONE CONNECTED WORKFLOW", size=44, bold=True, fill=GOLD_LIGHT)
    piano_roll(d, (650, 390, 1130, 565))
    staff(d, (75, 405, 585, 555))
    rr(d, (78, 575, 430, 615), 20, "#2A2112", GOLD, 1)
    label(d, (254, 595), "EARLY ACCESS • WINDOWS", size=19, bold=True, fill=GOLD_LIGHT, anchor="mm")
    save(im, "social-card-1200x630.png")


def background():
    im = gradient((1920, 1080), "#171108", "#070706").convert("RGBA")
    d = ImageDraw.Draw(im)
    for y in range(115, 965, 48):
        d.line((0, y, 1920, y), fill="#2D2518", width=1)
    for x in range(80, 1920, 160):
        d.line((x, 0, x, 1080), fill="#19150F", width=1)
    add_glow(im, (260, 160), 460, (210, 164, 58), 28)
    add_glow(im, (1700, 900), 520, (210, 164, 58), 18)
    save(im, "itch-background-1920x1080.png")


def main():
    cover()
    banner()
    feature_unified()
    feature_audio()
    feature_instruments()
    feature_midi()
    social()
    background()
    print(f"Generated 8 assets in {OUT}")


if __name__ == "__main__":
    main()
