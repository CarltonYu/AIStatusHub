#!/usr/bin/env python3
"""Render local GIF previews for the IrisOLED face player state machine."""

import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


SRC_W = 128
SRC_H = 64
SCREEN_W = 240
SCREEN_H = 240
FACE_W = 224
FACE_H = 112
COLOR = (0x00, 0xD7, 0xFF)
BACKGROUND = (0x00, 0x00, 0x00)


@dataclass
class Step:
    name: str
    duration_ms: int
    dx: int = 0
    dy: int = 0
    brightness: int = 255


def parse_names(header_path: Path):
    text = header_path.read_text(encoding="utf-8")
    return re.findall(r"extern\s+const\s+unsigned\s+char\s+(\w+)\[\]\s+PROGMEM", text)


def parse_bitmaps(cpp_path: Path):
    text = cpp_path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"const\s+unsigned\s+char\s+PROGMEM\s+(\w+)\[\]\s*=\s*\{(.*?)\};",
        re.S,
    )
    bitmaps = {}
    for name, body in pattern.findall(text):
        values = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", body)]
        if len(values) != SRC_W * SRC_H // 8:
            raise SystemExit(f"{name}: expected 1024 bytes, got {len(values)}")
        bitmaps[name] = values
    return bitmaps


def alias_name(name: str) -> str:
    if name == "idle":
        return "normal"
    if name in ("sleep", "sleeping"):
        return "sleepy"
    if name == "busy":
        return "focused"
    return name


def canonical_name(name: str, bitmaps) -> str:
    candidate = alias_name(name)
    if candidate in bitmaps:
        return candidate
    return "normal"


def add(seq, bitmaps, name, duration_ms, dx=0, dy=0, brightness=255):
    seq.append(Step(canonical_name(name, bitmaps), duration_ms, dx, dy, brightness))


def build_sequence(name: str, bitmaps):
    canon = canonical_name(name, bitmaps)
    seq = []
    if canon == "normal":
        add(seq, bitmaps, "normal", 2600)
        add(seq, bitmaps, "blink_down", 80, brightness=220)
        add(seq, bitmaps, "blink", 90, brightness=220)
        add(seq, bitmaps, "blink_up", 80, brightness=220)
        add(seq, bitmaps, "normal", 1800)
    elif canon == "sleepy":
        add(seq, bitmaps, "sleepy", 900, brightness=210)
        add(seq, bitmaps, "blink_down", 250, dy=1, brightness=160)
        add(seq, bitmaps, "blink", 900, dy=2, brightness=130)
        add(seq, bitmaps, "blink_up", 250, dy=1, brightness=160)
    elif canon == "blink":
        add(seq, bitmaps, "blink_down", 90)
        add(seq, bitmaps, "blink", 130)
        add(seq, bitmaps, "blink_up", 90)
    elif canon in ("happy", "excited"):
        add(seq, bitmaps, canon, 220)
        add(seq, bitmaps, canon, 140, dy=-4)
        add(seq, bitmaps, canon, 180)
        add(seq, bitmaps, canon, 140, dy=-2, brightness=235)
        add(seq, bitmaps, canon, 280)
    elif canon in ("angry", "furious"):
        add(seq, bitmaps, canon, 120, dx=-5)
        add(seq, bitmaps, canon, 120, dx=5)
        add(seq, bitmaps, canon, 120, dx=-3)
        add(seq, bitmaps, canon, 120, dx=3)
        add(seq, bitmaps, canon, 500)
    elif canon in ("focused", "alert"):
        add(seq, bitmaps, canon, 320)
        add(seq, bitmaps, canon, 180, brightness=220)
        add(seq, bitmaps, canon, 320)
    elif canon in ("disoriented", "scared"):
        add(seq, bitmaps, canon, 120, dx=-4, dy=-2)
        add(seq, bitmaps, canon, 120, dx=4, dy=2, brightness=230)
        add(seq, bitmaps, canon, 120, dx=-2, dy=1)
        add(seq, bitmaps, canon, 420, brightness=240)
    else:
        add(seq, bitmaps, canon, 1000)
    return seq


def bitmap_pixel(bitmap, x: int, y: int) -> bool:
    byte = bitmap[y * 16 + x // 8]
    return bool(byte & (0x80 >> (x & 7)))


def render_step(step: Step, bitmaps):
    img = Image.new("RGB", (SCREEN_W, SCREEN_H), BACKGROUND)
    pixels = img.load()
    bitmap = bitmaps[step.name]
    x0 = (SCREEN_W - FACE_W) // 2 + step.dx
    y0 = (SCREEN_H - FACE_H) // 2 + step.dy
    r = COLOR[0] * step.brightness // 255
    g = COLOR[1] * step.brightness // 255
    b = COLOR[2] * step.brightness // 255

    for y in range(FACE_H):
        sy = y * SRC_H // FACE_H
        py = y0 + y
        if py < 0 or py >= SCREEN_H:
            continue
        for x in range(FACE_W):
            sx = x * SRC_W // FACE_W
            if bitmap_pixel(bitmap, sx, sy):
                px = x0 + x
                if 0 <= px < SCREEN_W:
                    pixels[px, py] = (r, g, b)
    return img


def save_gif(name: str, seq, bitmaps, out_dir: Path):
    frames = [render_step(step, bitmaps) for step in seq]
    durations = [max(20, step.duration_ms) for step in seq]
    out_path = out_dir / f"{name}.gif"
    frames[0].save(
        out_path,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        disposal=2,
    )
    return out_path


def write_index(out_dir: Path, names, aliases, seq_info):
    lines = [
        "# IrisOLED Face GIF Previews",
        "",
        "Generated from the current `irisoled-face` state machine.",
        "",
        "| Name | Frames | Duration | GIF |",
        "| --- | ---: | ---: | --- |",
    ]
    for name in names + aliases:
        steps = seq_info[name]
        duration = sum(step.duration_ms for step in steps)
        lines.append(f"| `{name}` | {len(steps)} | {duration} ms | [{name}.gif]({name}.gif) |")
    (out_dir / "INDEX.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    cards = []
    for name in names + aliases:
        steps = seq_info[name]
        duration = sum(step.duration_ms for step in steps)
        cards.append(
            f"""
            <figure>
              <img src="{name}.gif" alt="{name}">
              <figcaption><code>{name}</code><br>{len(steps)} frame(s), {duration} ms</figcaption>
            </figure>
            """
        )

    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>IrisOLED Face GIF Previews</title>
  <style>
    body {{
      margin: 0;
      background: #101214;
      color: #e8eef2;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }}
    main {{
      max-width: 1320px;
      margin: 0 auto;
      padding: 24px;
    }}
    h1 {{
      margin: 0 0 8px;
      font-size: 24px;
      font-weight: 650;
    }}
    p {{
      margin: 0 0 20px;
      color: #aeb9c2;
    }}
    .grid {{
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
      gap: 16px;
    }}
    figure {{
      margin: 0;
      padding: 12px;
      background: #181c20;
      border: 1px solid #2a3138;
      border-radius: 8px;
    }}
    img {{
      width: 100%;
      image-rendering: pixelated;
      background: #000;
      display: block;
    }}
    figcaption {{
      margin-top: 8px;
      color: #b8c2ca;
      font-size: 13px;
      line-height: 1.35;
    }}
    code {{
      color: #8be9fd;
    }}
  </style>
</head>
<body>
  <main>
    <h1>IrisOLED Face GIF Previews</h1>
    <p>Rendered from the current <code>irisoled-face</code> state machine at 240x240.</p>
    <section class="grid">
      {''.join(cards)}
    </section>
  </main>
</body>
</html>
"""
    (out_dir / "index.html").write_text(html, encoding="utf-8")


def main():
    script_dir = Path(__file__).resolve().parent
    face_dir = script_dir.parent
    repo_root = face_dir.parents[3]
    default_irisoled = repo_root / "duo" / "CV1800B" / "eyes-resources" / "Irisoled"
    irisoled_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else default_irisoled
    out_dir = face_dir / "previews"

    header_path = irisoled_dir / "src" / "Irisoled.h"
    cpp_path = irisoled_dir / "src" / "Irisoled.cpp"
    names = parse_names(header_path)
    bitmaps = parse_bitmaps(cpp_path)
    aliases = ["idle", "sleep", "sleeping", "busy"]
    out_dir.mkdir(parents=True, exist_ok=True)

    seq_info = {}
    for name in names + aliases:
        seq = build_sequence(name, bitmaps)
        seq_info[name] = seq
        out_path = save_gif(name, seq, bitmaps, out_dir)
        print(f"{out_path} ({len(seq)} frames)")

    write_index(out_dir, names, aliases, seq_info)
    print(f"index: {out_dir / 'INDEX.md'}")


if __name__ == "__main__":
    main()
