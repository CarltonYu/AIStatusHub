#!/usr/bin/env python3
"""Generate multiple eye frames showing animation states."""

import os
from PIL import Image
from parse_eye_data import parse_h_file, render_eye

h_path = 'openemo/AnimatedEyes/data/default_large.h'
data = parse_h_file(h_path)

os.makedirs('parsed/frames', exist_ok=True)

frames = [
    ('normal', {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0, 'lower_t': 254}),
    ('blink_half', {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 128, 'lower_t': 128}),
    ('blink_closed', {'eye_x': 67, 'eye_y': 67, 'i_scale': 200, 'upper_t': 254, 'lower_t': 0}),
    ('pupil_small', {'eye_x': 67, 'eye_y': 67, 'i_scale': 150, 'upper_t': 0, 'lower_t': 254}),
    ('pupil_large', {'eye_x': 67, 'eye_y': 67, 'i_scale': 250, 'upper_t': 0, 'lower_t': 254}),
    ('look_left', {'eye_x': 30, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0, 'lower_t': 254}),
    ('look_right', {'eye_x': 110, 'eye_y': 67, 'i_scale': 200, 'upper_t': 0, 'lower_t': 254}),
    ('look_up', {'eye_x': 67, 'eye_y': 30, 'i_scale': 200, 'upper_t': 0, 'lower_t': 254}),
    ('look_down', {'eye_x': 67, 'eye_y': 110, 'i_scale': 200, 'upper_t': 0, 'lower_t': 254}),
]

for name, kwargs in frames:
    img = render_eye(data, **kwargs)
    pil_img = Image.fromarray(img)
    path = f'parsed/frames/{name}.png'
    pil_img.save(path)
    print(f"Saved {path}")
