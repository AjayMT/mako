#!/usr/bin/env python3

import argparse
from PIL import Image


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("input_file", help="Image file name")
    parser.add_argument("bitmap_name", help="Bitmap name used in generated macros and values")
    flags = parser.add_mutually_exclusive_group()
    flags.add_argument(
        "--cursor",
        "-c",
        action="store_true",
        help="Generate a mouse cursor bitmap that has only 3 enum color values",
    )
    flags.add_argument(
        "--alpha", "-a", action="store_true", help="Generate a bitmap of only the alpha channel"
    )
    return parser.parse_args()


def main():
    opts = get_options()
    image = Image.open(opts.input_file)
    w, h = image.size
    print(f"#define {opts.bitmap_name.upper()}_WIDTH  {w}")
    print(f"#define {opts.bitmap_name.upper()}_HEIGHT {h}")
    pixel_type = "uint8_t" if opts.cursor or opts.alpha else "uint32_t"
    print(f"static const {pixel_type} {opts.bitmap_name.upper()}_PIXELS[] = {{")

    if opts.cursor:

        def encode(p):
            if p[3] == 0:
                return "CURSOR_NONE"
            if p[0] != 0:
                return "CURSOR_WHITE"
            return "CURSOR_BLACK"

    elif opts.alpha:
        encode = lambda p: hex(p[3])
    else:
        encode = lambda p: hex(p[2] | (p[1] << 8) | (p[0] << 16) | (p[3] << 24))

    print(", ".join(encode(p) for p in image.getdata()))
    print("};")


if __name__ == "__main__":
    main()
