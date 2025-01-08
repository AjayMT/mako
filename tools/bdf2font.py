#!/usr/bin/env python3

"""BDF to C-array bitmap font compiler."""

import argparse
import bdfparser


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("font_filename")
    parser.add_argument("font_name")
    return parser.parse_args()


def main():
    opts = get_options()
    font = bdfparser.Font(opts.font_filename)
    height = font.headers["fbby"]
    char_info = []
    data_offset = 0

    print(f"#define {opts.font_name.upper()}_HEIGHT {height}")
    print(f"static uint8_t {opts.font_name.lower()}_data[] = {{")
    for i in range(32, 127):
        glyph = font.glyphbycp(i)
        bitmap = glyph.draw()
        char_info.append({"width": bitmap.width(), "data_offset": data_offset})
        chrbytes = bitmap.tobytes("L")
        print(", ".join(hex(0xFF - b) for b in chrbytes) + ",")
        data_offset += len(chrbytes)

    print("};")
    print(f"static struct font_char_info {opts.font_name.lower()}_char_info[] = {{")
    for info in char_info:
        print(f"  {{ .width = {info['width']}, .data_offset = {info['data_offset']} }},")

    print("};")


if __name__ == "__main__":
    main()
