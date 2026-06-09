#!/usr/bin/env python3
"""Generate a binary file with pre-rendered eye animation frames."""

import os
import sys
import struct
import numpy as np
from PIL import Image
from parse_eye_data import parse_h_file, render_eye

# Output resolution (default 240x240). Smaller sizes improve FPS on Duo
# because each SPI frame has fewer bytes and fewer ioctl chunks.
SCREEN_W = int(sys.argv[1]) if len(sys.argv) > 1 else 240
SCREEN_H = SCREEN_W

h_path = 'openemo/AnimatedEyes/data/default_large.h'
data = parse_h_file(h_path)

# Animation sequence: (name, kwargs, hold_frames)
# Threshold semantics from Adafruit Uncanny Eyes:
#   upper[idx] <= upper_t  OR  lower[idx] <= lower_t  -> covered by eyelid
# Smaller threshold = more open.  0 = fully open, 254 = fully closed.
sequence = [
    ('normal',      {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   8),
    ('blink_half',  {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 128, 'lower_t': 128}, 2),
    ('blink_closed',{'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 254, 'lower_t': 254}, 2),
    ('blink_half2', {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 128, 'lower_t': 128}, 2),
    ('normal2',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('look_left',   {'eye_x': 30, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('normal3',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('look_right',  {'eye_x': 110,'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('normal4',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('look_up',     {'eye_x': 67, 'eye_y': 30, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('normal5',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('look_down',   {'eye_x': 67, 'eye_y': 110,'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('normal6',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('pupil_small', {'eye_x': 67, 'eye_y': 67, 'i_scale': 150, 'upper_t': 0,   'lower_t': 0},   5),
    ('normal7',     {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 0},   5),
    ('pupil_large', {'eye_x': 67, 'eye_y': 67, 'i_scale': 250, 'upper_t': 0,   'lower_t': 0},   5),
]

FRAME_SIZE = SCREEN_W * SCREEN_H * 2

all_frames = bytearray()
frame_count = 0
for name, kwargs, hold in sequence:
    img = render_eye(data, **kwargs)
    if SCREEN_W != 240 or SCREEN_H != 240:
        pil = Image.fromarray(img)
        pil = pil.resize((SCREEN_W, SCREEN_H), Image.LANCZOS)
        img = np.array(pil)

    r = img[:, :, 0].astype(np.uint16)
    g = img[:, :, 1].astype(np.uint16)
    b = img[:, :, 2].astype(np.uint16)
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    rgb565_flat = rgb565.flatten()
    buf = bytearray(FRAME_SIZE)
    buf[0::2] = ((rgb565_flat >> 8) & 0xFF).astype(np.uint8).tobytes()
    buf[1::2] = (rgb565_flat & 0xFF).astype(np.uint8).tobytes()
    for _ in range(hold):
        all_frames.extend(buf)
        frame_count += 1

os.makedirs('parsed', exist_ok=True)
out_path = f'parsed/eye_animation_{SCREEN_W}x{SCREEN_H}.bin'
with open(out_path, 'wb') as f:
    f.write(struct.pack('<I', frame_count))
    f.write(struct.pack('<I', FRAME_SIZE))
    f.write(struct.pack('<HH', SCREEN_W, SCREEN_H))
    f.write(all_frames)

print(f"Total frames: {frame_count}, resolution: {SCREEN_W}x{SCREEN_H}")
print(f"Saved {out_path} ({12 + len(all_frames)} bytes with header)")
