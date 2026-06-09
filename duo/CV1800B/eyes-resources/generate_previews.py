#!/usr/bin/env python3
"""
Generate eye preview images from Adafruit Uncanny Eyes PNG sources.
Each eye directory contains:
  - sclera.png
  - iris.png
  - lid-upper.png
  - lid-lower.png
  - (optional) pupilMap.png
"""

import os
import sys
from PIL import Image

BASE_DIR = "adafruit-uncanny-eyes/convert"
OUTPUT_DIR = "previews"

EYES = [
    "catEye", "defaultEye", "doeEye", "dragonEye", "goatEye",
    "naugaEye", "newtEye", "noScleraEye", "terminatorEye"
]

os.makedirs(OUTPUT_DIR, exist_ok=True)


def compose_eye(eye_name):
    eye_dir = os.path.join(BASE_DIR, eye_name)
    if not os.path.isdir(eye_dir):
        print(f"Skip {eye_name}: directory not found")
        return None

    # Load images
    sclera_path = os.path.join(eye_dir, "sclera.png")
    iris_path = os.path.join(eye_dir, "iris.png")
    upper_path = os.path.join(eye_dir, "lid-upper.png")
    lower_path = os.path.join(eye_dir, "lid-lower.png")

    if not all(os.path.exists(p) for p in [sclera_path, iris_path, upper_path, lower_path]):
        print(f"Skip {eye_name}: missing PNG files")
        return None

    sclera = Image.open(sclera_path).convert("RGBA")
    iris = Image.open(iris_path).convert("RGBA")
    upper = Image.open(upper_path).convert("RGBA")
    lower = Image.open(lower_path).convert("RGBA")

    # Create canvas (use sclera size, scaled up for visibility)
    canvas_size = (240, 240)
    canvas = Image.new("RGBA", canvas_size, (0, 0, 0, 0))

    # Scale sclera to fit canvas
    sclera_scaled = sclera.resize(canvas_size, Image.LANCZOS)
    canvas.paste(sclera_scaled, (0, 0), sclera_scaled)

    # Scale and center iris
    iris_size = (canvas_size[0] // 2, canvas_size[1] // 2)
    iris_scaled = iris.resize(iris_size, Image.LANCZOS)
    iris_pos = ((canvas_size[0] - iris_size[0]) // 2, (canvas_size[1] - iris_size[1]) // 2)
    canvas.paste(iris_scaled, iris_pos, iris_scaled)

    # Scale and overlay eyelids
    upper_scaled = upper.resize(canvas_size, Image.LANCZOS)
    lower_scaled = lower.resize(canvas_size, Image.LANCZOS)
    canvas.paste(upper_scaled, (0, 0), upper_scaled)
    canvas.paste(lower_scaled, (0, 0), lower_scaled)

    # Convert to RGB and save
    rgb = Image.new("RGB", canvas_size, (0, 0, 0))
    rgb.paste(canvas, (0, 0), canvas)

    output_path = os.path.join(OUTPUT_DIR, f"{eye_name}.png")
    rgb.save(output_path)
    print(f"Saved {output_path}")
    return output_path


def show_sources():
    """Also generate a grid of source thumbnails for quick browsing"""
    for eye_name in EYES:
        eye_dir = os.path.join(BASE_DIR, eye_name)
        if not os.path.isdir(eye_dir):
            continue
        for fname in ["sclera.png", "iris.png", "lid-upper.png", "lid-lower.png"]:
            fpath = os.path.join(eye_dir, fname)
            if os.path.exists(fpath):
                img = Image.open(fpath).convert("RGBA")
                img.thumbnail((120, 120))
                out = os.path.join(OUTPUT_DIR, f"{eye_name}_{fname}")
                img.save(out)


if __name__ == "__main__":
    for eye in EYES:
        compose_eye(eye)
    print("Done")
