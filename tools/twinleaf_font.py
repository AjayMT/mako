#!/usr/bin/env python3

import argparse
import os
from PIL import Image


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("png_dir", help="Directory containing PNG files")
    parser.add_argument("font_name", help="Font name used in generated macros and values")
    return parser.parse_args()


def encode(p):
    return hex(p[3])


def shadows(data, w, h):
    for y in range(h):
        for x in range(w):
            if data[y * w + x][3] != 0xFF:
                continue

            p = data[y * w + x + 1]
            data[y * w + x + 1] = (p[0], p[1], p[2], max(p[3], 64))
            p = data[(y + 1) * w + x + 1]
            data[(y + 1) * w + x + 1] = (p[0], p[1], p[2], max(p[3], 64))
            p = data[(y + 1) * w + x]
            data[(y + 1) * w + x] = (p[0], p[1], p[2], max(p[3], 64))


def main():
    opts = get_options()
    char_infos = []
    height = Image.open(os.path.join(opts.png_dir, "32.png")).size[1]
    data_offset = 0

    print(f"#define {opts.font_name.upper()}_HEIGHT {height}")
    print(f"static uint8_t {opts.font_name.lower()}_data[] = {{")
    for c in range(32, 127):
        img = Image.open(os.path.join(opts.png_dir, f"{c}.png"))
        w, h = img.size
        img_data = list(img.getdata())
        shadows(img_data, w, h)
        for row in range(h):
            print(
                "0x0, " + ", ".join(encode(p) for p in img_data[(row * w) : ((row + 1) * w)]) + ","
            )

        char_infos.append({"width": w + 1, "data_offset": data_offset})
        data_offset += len(img_data) + h

    print("};")
    print(f"static struct font_char_info {opts.font_name.lower()}_char_info[] = {{")
    for info in char_infos:
        print(f"  {{ .width = {info['width']}, .data_offset = {info['data_offset']} }},")

    print("};")


if __name__ == "__main__":
    main()
