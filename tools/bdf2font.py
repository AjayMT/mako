#!/usr/bin/env python3

"""BDF to C-array bitmap font compiler."""

import argparse
import bdfparser


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("font_filename")
    parser.add_argument("font_name")
    return parser.parse_args()


def shadows(data, w, h):
    shadow_opacity = 64
    new_data = [0] * ((w + 1) * (h + 1))
    for y in range(h):
        for x in range(w):
            new_data[y * (w + 1) + x] = max(new_data[y * (w + 1) + x], data[y * w + x])
            if data[y * w + x] == 0:
                continue

            new_data[y * (w + 1) + x + 1] = max(new_data[y * (w + 1) + x + 1], shadow_opacity)
            new_data[(y + 1) * (w + 1) + x] = max(new_data[(y + 1) * (w + 1) + x], shadow_opacity)
            new_data[(y + 1) * (w + 1) + x + 1] = max(
                new_data[(y + 1) * (w + 1) + x + 1], shadow_opacity
            )

    return new_data


def main():
    opts = get_options()
    font = bdfparser.Font(opts.font_filename)
    height = font.headers["fbby"] + 1
    char_info = []
    data_offset = 0

    print(f"#define {opts.font_name.upper()}_HEIGHT {height}")
    print(f"static uint8_t {opts.font_name.lower()}_data[] = {{")
    for i in range(32, 127):
        glyph = font.glyphbycp(i)
        bitmap = glyph.draw()
        chrbytes = shadows([0xFF - b for b in bitmap.tobytes("L")], bitmap.width(), height - 1)
        print(", ".join(hex(b) for b in chrbytes) + ",")
        char_info.append({"width": bitmap.width() + 1, "data_offset": data_offset})
        data_offset += len(chrbytes)

    print("};")
    print(f"static struct font_char_info {opts.font_name.lower()}_char_info[] = {{")
    for info in char_info:
        print(f"  {{ .width = {info['width']}, .data_offset = {info['data_offset']} }},")

    print("};")


if __name__ == "__main__":
    main()
