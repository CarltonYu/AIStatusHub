#!/usr/bin/env python3
"""Generate a smooth 240x240 RGB565 eye animation for the Duo LCD."""

import math
import os
import struct
import sys

import numpy as np
from PIL import Image

from parse_eye_data import parse_h_file, render_eye


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


def smoothstep(x):
    x = clamp(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def wrap_dist(a, b):
    d = abs(a - b)
    return min(d, 1.0 - d)


def pulse(t, center, width):
    d = wrap_dist(t, center)
    if d >= width:
        return 0.0
    x = d / width
    return 0.5 + 0.5 * math.cos(math.pi * x)


def interp_keys(keys, t):
    for idx in range(len(keys) - 1):
        t0, x0, y0 = keys[idx]
        t1, x1, y1 = keys[idx + 1]
        if t0 <= t <= t1:
            span = max(t1 - t0, 1e-6)
            u = smoothstep((t - t0) / span)
            return x0 + (x1 - x0) * u, y0 + (y1 - y0) * u
    return keys[-1][1], keys[-1][2]


def rgb888_to_rgb565_be(img):
    r = img[:, :, 0].astype(np.uint16)
    g = img[:, :, 1].astype(np.uint16)
    b = img[:, :, 2].astype(np.uint16)
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    flat = rgb565.flatten()
    out = bytearray(flat.size * 2)
    out[0::2] = ((flat >> 8) & 0xFF).astype(np.uint8).tobytes()
    out[1::2] = (flat & 0xFF).astype(np.uint8).tobytes()
    return out


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    size = int(sys.argv[1]) if len(sys.argv) > 1 else 240
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 240
    out_path = (
        sys.argv[3]
        if len(sys.argv) > 3
        else f"parsed/eye_animation_{size}x{size}_smooth_{frames}f.bin"
    )

    data = parse_h_file("openemo/AnimatedEyes/data/default_large.h")
    frame_size = size * size * 2

    gaze_keys = [
        (0.00, 67, 67),
        (0.08, 67, 67),
        (0.14, 98, 57),
        (0.21, 98, 57),
        (0.29, 38, 76),
        (0.36, 38, 76),
        (0.45, 67, 67),
        (0.53, 83, 104),
        (0.61, 83, 104),
        (0.71, 108, 48),
        (0.79, 108, 48),
        (0.90, 67, 67),
        (1.00, 67, 67),
    ]

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    preview = None
    all_frames = bytearray()

    for i in range(frames):
        t = i / frames
        eye_x, eye_y = interp_keys(gaze_keys, t)
        micro_x = 3.0 * math.sin(2.0 * math.pi * (t * 7.0 + 0.13))
        micro_y = 2.0 * math.sin(2.0 * math.pi * (t * 5.0 + 0.37))

        blink = max(
            pulse(t, 0.18, 0.025),
            pulse(t, 0.56, 0.021),
            pulse(t, 0.84, 0.032),
        )
        pupil = 205 + 28 * math.sin(2.0 * math.pi * (t * 1.7 + 0.11))
        pupil -= 18 * blink

        img = render_eye(
            data,
            eye_x=int(round(clamp(eye_x + micro_x, 30, 110))),
            eye_y=int(round(clamp(eye_y + micro_y, 30, 110))),
            i_scale=int(round(clamp(pupil, 150, 250))),
            upper_t=int(round(254 * blink)),
            lower_t=int(round(254 * blink)),
        )

        if size != 240:
            pil = Image.fromarray(img)
            pil = pil.resize((size, size), Image.LANCZOS)
            img = np.array(pil)

        if preview is None:
            preview = Image.fromarray(img)

        all_frames.extend(rgb888_to_rgb565_be(img))

        if (i + 1) % 20 == 0 or i + 1 == frames:
            print(f"rendered {i + 1}/{frames} frames")

    with open(out_path, "wb") as f:
        f.write(struct.pack("<IIHH", frames, frame_size, size, size))
        f.write(all_frames)

    preview_path = os.path.splitext(out_path)[0] + "_preview.png"
    if preview:
        preview.save(preview_path)

    total = os.path.getsize(out_path)
    print(f"Saved {out_path}")
    print(f"Frames: {frames}, resolution: {size}x{size}, bytes: {total}")
    print(f"Preview: {preview_path}")


if __name__ == "__main__":
    main()
