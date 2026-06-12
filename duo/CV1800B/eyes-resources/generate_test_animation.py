#!/usr/bin/env python3
"""Generate a simple test RGB565 animation for GC9A01 240x240 eye_anim."""

import math
import struct
import sys

import numpy as np
from PIL import Image


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


def draw_frame(width, height, t, frame_idx):
    """Draw a simple animated color ring on black background."""
    y, x = np.ogrid[:height, :width]
    cx, cy = width // 2, height // 2
    dx = x - cx
    dy = y - cy
    dist = np.sqrt(dx * dx + dy * dy)
    angle = np.arctan2(dy, dx)

    # Rotating hue
    hue = (angle / (2 * math.pi) + t) % 1.0
    sat = np.clip(dist / (width * 0.45), 0, 1)
    val = np.where(dist < width * 0.48, 1.0, 0.0)

    h = hue * 6
    i = h.astype(np.int32)
    f = h - i
    p = val * (1 - sat)
    q = val * (1 - f * sat)
    r = np.zeros_like(hue)
    g = np.zeros_like(hue)
    b = np.zeros_like(hue)

    # HSV to RGB for each sector
    for idx in range(6):
        mask = (i % 6 == idx)
        if idx == 0:
            r[mask], g[mask], b[mask] = val[mask], q[mask], p[mask]
        elif idx == 1:
            r[mask], g[mask], b[mask] = q[mask], val[mask], p[mask]
        elif idx == 2:
            r[mask], g[mask], b[mask] = p[mask], val[mask], q[mask]
        elif idx == 3:
            r[mask], g[mask], b[mask] = p[mask], q[mask], val[mask]
        elif idx == 4:
            r[mask], g[mask], b[mask] = q[mask], p[mask], val[mask]
        else:
            r[mask], g[mask], b[mask] = val[mask], p[mask], q[mask]

    rgb = np.stack([r, g, b], axis=2) * 255
    return rgb.astype(np.uint8)


def main():
    width = 240
    height = 240
    frames = 60
    out_path = "test_animation_240x240.bin"
    if len(sys.argv) > 1:
        out_path = sys.argv[1]

    frame_size = width * height * 2
    data = bytearray()
    data += struct.pack("<I", frames)      # frame_count
    data += struct.pack("<I", frame_size)  # frame_size
    data += struct.pack("<H", width)       # width
    data += struct.pack("<H", height)      # height

    for i in range(frames):
        t = i / frames
        img = draw_frame(width, height, t, i)
        data += rgb888_to_rgb565_be(img)

    with open(out_path, "wb") as f:
        f.write(data)

    print(f"Generated {out_path}: {frames} frames, {width}x{height}, {len(data)} bytes")


if __name__ == "__main__":
    main()
