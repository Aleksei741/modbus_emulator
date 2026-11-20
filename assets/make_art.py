"""Generate Modbus-themed PNG icon and banner without external image APIs."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


def lerp(a: int, b: int, t: float) -> int:
    return int(a + (b - a) * t)


def mix(c1: tuple[int, int, int], c2: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return (lerp(c1[0], c2[0], t), lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t))


def rounded_rect(draw: ImageDraw.ImageDraw, box, radius: int, fill) -> None:
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def draw_led(draw: ImageDraw.ImageDraw, cx: int, cy: int, r: int, on: bool) -> None:
    if on:
        draw.ellipse((cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2), fill=(90, 50, 10))
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 176, 48))
        draw.ellipse((cx - r // 2, cy - r, cx + r // 3, cy - r // 4), fill=(255, 230, 160))
    else:
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(38, 44, 48))
        draw.ellipse((cx - r + 1, cy - r + 1, cx + r - 2, cy + r - 2), fill=(22, 26, 28))


def make_icon(path: Path, size: int = 512) -> None:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    bg = (18, 24, 28)
    plaque = (28, 36, 42)
    plaque_hi = (48, 62, 70)
    copper = (196, 122, 58)
    copper_dk = (128, 72, 28)
    teal = (32, 176, 168)
    teal_dk = (14, 90, 88)
    steel = (72, 84, 92)

    pad = size // 18
    rounded_rect(draw, (pad, pad, size - pad, size - pad), size // 8, plaque)
    # top highlight
    draw.pieslice((pad, pad, size - pad, pad + size // 3), 180, 360, fill=plaque_hi)
    rounded_rect(draw, (pad + 6, pad + 6, size - pad - 6, size - pad - 6), size // 9, plaque)

    # terminal block
    tb = (int(size * 0.16), int(size * 0.28), int(size * 0.46), int(size * 0.78))
    rounded_rect(draw, tb, 12, steel)
    draw.rectangle((tb[0] + 8, tb[1] + 10, tb[2] - 8, tb[3] - 10), fill=(40, 46, 50))
    for i in range(4):
        y = tb[1] + 28 + i * ((tb[3] - tb[1] - 56) // 3)
        draw.ellipse((tb[0] + 22, y - 10, tb[0] + 50, y + 18), fill=copper_dk)
        draw.ellipse((tb[0] + 26, y - 6, tb[0] + 46, y + 14), fill=copper)
        draw.rectangle((tb[0] + 32, y - 2, tb[0] + 40, y + 10), fill=(230, 190, 120))

    # PLC body
    plc = (int(size * 0.50), int(size * 0.24), int(size * 0.86), int(size * 0.76))
    rounded_rect(draw, plc, 16, (22, 30, 34))
    draw.rectangle((plc[0] + 10, plc[1] + 12, plc[2] - 10, plc[1] + 36), fill=teal_dk)
    draw.rectangle((plc[0] + 16, plc[1] + 18, plc[2] - 16, plc[1] + 30), fill=teal)

    # LED register grid 4x6
    cols, rows = 6, 4
    gx0, gy0 = plc[0] + 28, plc[1] + 58
    gapx = (plc[2] - plc[0] - 56) // (cols - 1)
    gapy = (plc[3] - plc[1] - 90) // (rows - 1)
    pattern = [
        [1, 0, 1, 1, 0, 1],
        [0, 1, 0, 1, 1, 0],
        [1, 1, 0, 0, 1, 1],
        [0, 1, 1, 0, 1, 0],
    ]
    for r in range(rows):
        for c in range(cols):
            draw_led(draw, gx0 + c * gapx, gy0 + r * gapy, 7, bool(pattern[r][c]))

    # cable
    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    x0, y0 = tb[2] - 4, (tb[1] + tb[3]) // 2
    x1, y1 = plc[0] + 4, (plc[1] + plc[3]) // 2 - 8
    od.line([(x0, y0), ((x0 + x1) // 2, y0 - 18), (x1, y1)], fill=teal + (255,), width=10)
    od.line([(x0, y0 + 10), ((x0 + x1) // 2, y0 - 6), (x1, y1 + 10)], fill=teal_dk + (255,), width=7)
    img = Image.alpha_composite(img, overlay)

    # inner shadow
    img = img.filter(ImageFilter.SMOOTH_MORE)
    img.save(path, "PNG")


def make_banner(path: Path, width: int = 1280, height: int = 220) -> None:
    img = Image.new("RGB", (width, height), (16, 20, 24))
    px = img.load()
    for y in range(height):
        t = y / max(height - 1, 1)
        col = mix((16, 22, 26), (8, 40, 42), t * 0.55)
        for x in range(width):
            wave = 0.08 * math.sin((x + y * 2) / 38.0)
            px[x, y] = mix(col, (20, 70, 68), max(0.0, wave))

    draw = ImageDraw.Draw(img)

    # DIN rail
    draw.rectangle((0, height // 2 + 18, width, height // 2 + 42), fill=(70, 78, 84))
    draw.rectangle((0, height // 2 + 22, width, height // 2 + 38), fill=(110, 118, 124))

    # PLC bricks
    for i, on_count in enumerate((3, 5, 2, 6, 4)):
        x = 80 + i * 230
        draw.rounded_rectangle((x, 36, x + 190, height - 28), 10, fill=(28, 36, 40))
        draw.rectangle((x + 14, 48, x + 176, 70), fill=(18, 110, 108))
        for b in range(8):
            bx = x + 28 + b * 18
            by = 96
            lit = b < on_count or (b + i) % 3 == 0
            draw.rectangle((bx, by, bx + 12, by + 18), fill=(255, 176, 48) if lit else (34, 40, 44))

    # cable
    draw.line([(0, height - 48), (width, height - 72)], fill=(32, 176, 168), width=8)
    draw.line([(0, height - 40), (width, height - 64)], fill=(14, 90, 88), width=5)

    img.save(path, "PNG")


def main() -> None:
    root = Path(__file__).resolve().parent
    make_icon(root / "modbus_icon.png")
    make_banner(root / "modbus_banner.png")
    print(f"wrote {root / 'modbus_icon.png'}")
    print(f"wrote {root / 'modbus_banner.png'}")


if __name__ == "__main__":
    main()
