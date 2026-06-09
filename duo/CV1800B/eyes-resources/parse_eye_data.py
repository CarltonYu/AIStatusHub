#!/usr/bin/env python3
"""
Parse OpenEmo/Halloween Skull eye data (.h files) and generate:
1. C header files with the eye data
2. Preview PNG images of rendered frames
"""

import re
import os
import sys
from PIL import Image
import numpy as np

# Map array names to their dimensions
# Dimensions are determined by SCLERA_WIDTH/HEIGHT, IRIS_WIDTH/HEIGHT, etc.
ARRAY_DIMS = {
    'sclera': ('SCLERA_WIDTH', 'SCLERA_HEIGHT'),
    'iris': ('IRIS_MAP_WIDTH', 'IRIS_MAP_HEIGHT'),
    'polar': ('IRIS_WIDTH', 'IRIS_HEIGHT'),
    'upper': ('SCREEN_WIDTH', 'SCREEN_HEIGHT'),
    'lower': ('SCREEN_WIDTH', 'SCREEN_HEIGHT'),
}


def parse_h_file(filepath):
    """Parse a .h eye data file and return dict of arrays and constants."""
    with open(filepath, 'r') as f:
        content = f.read()

    result = {'arrays': {}, 'consts': {}}

    # Parse #define constants
    for match in re.finditer(r'#define\s+(\w+)\s+(\d+)', content):
        result['consts'][match.group(1)] = int(match.group(2))

    # Parse const arrays: uint16_t name[N] = { ... } or uint8_t name[N] = { ... }
    pattern = r'const\s+(uint\d+_t)\s+(\w+)\s*\[[^\]]*\]\s*PROGMEM\s*=\s*\{([^}]+)\};'
    for match in re.finditer(pattern, content, re.DOTALL):
        dtype = match.group(1)
        name = match.group(2)
        data_str = match.group(3)

        # Extract hex/decimal values
        values = []
        for token in re.findall(r'0x[0-9a-fA-F]+|\d+', data_str):
            values.append(int(token, 0))

        result['arrays'][name] = {'dtype': dtype, 'data': values}

    # Also parse without PROGMEM
    pattern2 = r'const\s+(uint\d+_t)\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{([^}]+)\};'
    for match in re.finditer(pattern2, content, re.DOTALL):
        dtype = match.group(1)
        name = match.group(2)
        if name in result['arrays']:
            continue
        data_str = match.group(3)
        values = []
        for token in re.findall(r'0x[0-9a-fA-F]+|\d+', data_str):
            values.append(int(token, 0))
        result['arrays'][name] = {'dtype': dtype, 'data': values}

    return result


def rgb565_to_rgb888(pixel):
    """Convert RGB565 to RGB888 tuple."""
    r = ((pixel >> 11) & 0x1F) << 3
    g = ((pixel >> 5) & 0x3F) << 2
    b = (pixel & 0x1F) << 3
    return (r, g, b)


def render_eye(eye_data, eye_x=None, eye_y=None, i_scale=None, upper_t=0, lower_t=254):
    """Render one frame of the eye."""
    consts = eye_data['consts']
    arrs = eye_data['arrays']

    SW = consts.get('SCREEN_WIDTH', 240)
    SH = consts.get('SCREEN_HEIGHT', 240)
    SCLERA_W = consts['SCLERA_WIDTH']
    SCLERA_H = consts['SCLERA_HEIGHT']
    IRIS_W = consts['IRIS_WIDTH']
    IRIS_H = consts['IRIS_HEIGHT']
    IRIS_MAP_W = consts['IRIS_MAP_WIDTH']
    IRIS_MAP_H = consts['IRIS_MAP_HEIGHT']

    sclera = arrs['sclera']['data']
    iris = arrs['iris']['data']
    polar = arrs['polar']['data']
    upper = arrs['upper']['data']
    lower = arrs['lower']['data']

    # Default to center position
    if eye_x is None:
        eye_x = (SCLERA_W - SW) // 2
    if eye_y is None:
        eye_y = (SCLERA_H - SH) // 2
    if i_scale is None:
        i_scale = (consts.get('IRIS_MIN', 150) + consts.get('IRIS_MAX', 250)) // 2

    iris_threshold = (128 * (1023 - i_scale) + 512) // 1024
    iris_scale = IRIS_MAP_H * 65536 // iris_threshold

    img = np.zeros((SH, SW, 3), dtype=np.uint8)

    sclera_x_save = eye_x
    iris_y = eye_y - (SCLERA_H - IRIS_H) // 2

    for screen_y in range(SH):
        sclera_y = eye_y + screen_y
        iris_y_cur = iris_y + screen_y
        sclera_x = sclera_x_save
        iris_x = sclera_x_save - (SCLERA_W - IRIS_W) // 2
        lid_x = SW - 1  # left eye

        for screen_x in range(SW):
            lid_idx = screen_y * SW + lid_x
            lid_x -= 1

            if (lower[lid_idx] <= lower_t) or (upper[lid_idx] <= upper_t):
                p = 0
            elif (iris_y_cur < 0) or (iris_y_cur >= IRIS_H) or (iris_x < 0) or (iris_x >= IRIS_W):
                sclera_idx = sclera_y * SCLERA_W + sclera_x
                p = sclera[sclera_idx]
            else:
                polar_idx = iris_y_cur * IRIS_W + iris_x
                p = polar[polar_idx]
                d = (i_scale * (p & 0x7F)) // 128
                if d < IRIS_MAP_H:
                    a = (IRIS_MAP_W * (p >> 7)) // 512
                    iris_idx = d * IRIS_MAP_W + a
                    p = iris[iris_idx]
                else:
                    sclera_idx = sclera_y * SCLERA_W + sclera_x
                    p = sclera[sclera_idx]

            r, g, b = rgb565_to_rgb888(p)
            img[screen_y, screen_x] = [r, g, b]

            sclera_x += 1
            iris_x += 1

    return img


def save_c_header(eye_data, eye_name, output_path):
    """Save parsed eye data as a C header file."""
    with open(output_path, 'w') as f:
        f.write(f'#ifndef {eye_name.upper()}_EYE_H\n')
        f.write(f'#define {eye_name.upper()}_EYE_H\n\n')
        f.write('#include <stdint.h>\n\n')

        for k, v in eye_data['consts'].items():
            f.write(f'#define {k} {v}\n')
        f.write('\n')

        for name, arr in eye_data['arrays'].items():
            dtype = arr['dtype']
            data = arr['data']
            f.write(f'const {dtype} {name}[] = {{\n')
            for i in range(0, len(data), 8):
                line = ', '.join(f'0x{v:04x}' if dtype == 'uint16_t' else f'0x{v:02x}'
                                for v in data[i:i+8])
                f.write(f'  {line},\n')
            f.write('};\n\n')

        f.write(f'#endif // {eye_name.upper()}_EYE_H\n')


if __name__ == '__main__':
    # Parse default_large.h
    h_path = 'openemo/AnimatedEyes/data/default_large.h'
    if not os.path.exists(h_path):
        print(f"File not found: {h_path}")
        sys.exit(1)

    data = parse_h_file(h_path)
    print("Constants:", data['consts'])
    print("Arrays found:", list(data['arrays'].keys()))
    for name, arr in data['arrays'].items():
        print(f"  {name}: {arr['dtype']}, {len(arr['data'])} values")

    # Save C header
    os.makedirs('parsed', exist_ok=True)
    save_c_header(data, 'default_large', 'parsed/default_large_eye.h')
    print("Saved parsed/default_large_eye.h")

    # Render a preview frame
    img = render_eye(data, eye_x=67, eye_y=67, i_scale=200, upper_t=0, lower_t=254)
    pil_img = Image.fromarray(img)
    pil_img.save('parsed/default_large_preview.png')
    print("Saved parsed/default_large_preview.png")
