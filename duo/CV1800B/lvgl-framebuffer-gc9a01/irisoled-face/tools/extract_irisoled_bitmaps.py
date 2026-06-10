#!/usr/bin/env python3
"""Convert IrisOLED Arduino PROGMEM bitmaps into a plain C header."""

import os
import re
import sys


SRC_W = 128
SRC_H = 64
BYTES_PER_BITMAP = SRC_W * SRC_H // 8


def parse_names(header_path):
    text = open(header_path, "r", encoding="utf-8").read()
    return re.findall(r"extern\s+const\s+unsigned\s+char\s+(\w+)\[\]\s+PROGMEM", text)


def parse_arrays(cpp_path):
    text = open(cpp_path, "r", encoding="utf-8").read()
    pattern = re.compile(
        r"const\s+unsigned\s+char\s+PROGMEM\s+(\w+)\[\]\s*=\s*\{(.*?)\};",
        re.S,
    )
    arrays = {}
    for name, body in pattern.findall(text):
        values = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", body)]
        if len(values) != BYTES_PER_BITMAP:
            raise SystemExit(f"{name}: expected {BYTES_PER_BITMAP} bytes, got {len(values)}")
        arrays[name] = values
    return arrays


def write_header(out_path, names, arrays, commit):
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/* Generated from orji123/Irisoled MIT-licensed bitmap data. */\n")
        f.write(f"/* Source commit: {commit} */\n")
        f.write("#ifndef IRISOLED_BITMAPS_GENERATED_H\n")
        f.write("#define IRISOLED_BITMAPS_GENERATED_H\n\n")
        f.write("#include <stddef.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define IRISOLED_BITMAP_WIDTH 128\n")
        f.write("#define IRISOLED_BITMAP_HEIGHT 64\n")
        f.write("#define IRISOLED_BITMAP_BYTES 1024\n\n")

        for name in names:
            values = arrays[name]
            f.write(f"static const uint8_t irisoled_{name}[IRISOLED_BITMAP_BYTES] = {{\n")
            for i in range(0, len(values), 16):
                chunk = ", ".join(f"0x{x:02x}" for x in values[i : i + 16])
                f.write(f"    {chunk},\n")
            f.write("};\n\n")

        f.write("struct irisoled_bitmap_entry {\n")
        f.write("    const char *name;\n")
        f.write("    const uint8_t *data;\n")
        f.write("};\n\n")
        f.write("static const struct irisoled_bitmap_entry irisoled_bitmaps[] = {\n")
        for name in names:
            f.write(f'    {{ "{name}", irisoled_{name} }},\n')
        f.write("};\n\n")
        f.write(
            "static const size_t irisoled_bitmap_count = "
            "sizeof(irisoled_bitmaps) / sizeof(irisoled_bitmaps[0]);\n\n"
        )
        f.write("#endif\n")


def main():
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: extract_irisoled_bitmaps.py <Irisoled.h> <Irisoled.cpp> <commit> <out.h>"
        )

    header_path, cpp_path, commit, out_path = sys.argv[1:]
    names = parse_names(header_path)
    arrays = parse_arrays(cpp_path)
    missing = [name for name in names if name not in arrays]
    if missing:
        raise SystemExit(f"missing arrays: {', '.join(missing)}")

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    write_header(out_path, names, arrays, commit)
    print(f"generated {out_path} with {len(names)} bitmaps")


if __name__ == "__main__":
    main()
