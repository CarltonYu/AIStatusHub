#!/usr/bin/env python3
"""Generate a C header file with pre-rendered eye animation frames."""

import os
import numpy as np
from parse_eye_data import parse_h_file, render_eye

h_path = 'openemo/AnimatedEyes/data/default_large.h'
data = parse_h_file(h_path)

# Animation sequence: (name, kwargs, hold_frames)
# hold_frames controls how many times this frame repeats
sequence = [
    ('normal',    {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 8),
    ('blink_half',{'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 128, 'lower_t': 128}, 2),
    ('blink_closed',{'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 254, 'lower_t': 0}, 2),
    ('blink_half2',{'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 128, 'lower_t': 128}, 2),
    ('normal2',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('look_left', {'eye_x': 30, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('normal3',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('look_right',{'eye_x': 110,'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('normal4',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('look_up',   {'eye_x': 67, 'eye_y': 30, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('normal5',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('look_down', {'eye_x': 67, 'eye_y': 110,'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('normal6',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('pupil_small',{'eye_x': 67,'eye_y': 67, 'i_scale': 150, 'upper_t': 0,   'lower_t': 254}, 5),
    ('normal7',   {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0,   'lower_t': 254}, 5),
    ('pupil_large',{'eye_x': 67,'eye_y': 67, 'i_scale': 250, 'upper_t': 0,   'lower_t': 254}, 5),
]

SCREEN_W = 240
SCREEN_H = 240
FRAME_SIZE = SCREEN_W * SCREEN_H * 2  # RGB565

all_frames = []
frame_count = 0
for name, kwargs, hold in sequence:
    img = render_eye(data, **kwargs)
    r = img[:, :, 0].astype(np.uint16)
    g = img[:, :, 1].astype(np.uint16)
    b = img[:, :, 2].astype(np.uint16)
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    rgb565_flat = rgb565.flatten()
    buf = np.zeros(FRAME_SIZE, dtype=np.uint8)
    buf[0::2] = (rgb565_flat >> 8) & 0xFF
    buf[1::2] = rgb565_flat & 0xFF
    for _ in range(hold):
        all_frames.extend(buf.tolist())
        frame_count += 1

print(f"Total frames: {frame_count}, total bytes: {len(all_frames)}")

os.makedirs('parsed', exist_ok=True)
with open('parsed/eye_animation.h', 'w') as f:
    f.write('#ifndef EYE_ANIMATION_H\n')
    f.write('#define EYE_ANIMATION_H\n\n')
    f.write('#include <stdint.h>\n\n')
    f.write(f'#define ANIMATION_FRAME_COUNT {frame_count}\n')
    f.write(f'#define ANIMATION_FRAME_SIZE {FRAME_SIZE}\n')
    f.write(f'#define ANIMATION_WIDTH {SCREEN_W}\n')
    f.write(f'#define ANIMATION_HEIGHT {SCREEN_H}\n\n')
    f.write('const uint8_t eye_animation_data[] = {\n')
    for i in range(0, len(all_frames), 12):
        line = ', '.join(f'0x{b:02x}' for b in all_frames[i:i+12])
        f.write(f'  {line},\n')
    f.write('};\n\n')
    f.write('#endif // EYE_ANIMATION_H\n')

print("Saved parsed/eye_animation.h")
