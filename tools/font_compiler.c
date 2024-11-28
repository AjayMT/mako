
// TTF -> C array font compiler using stb_truetype.

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct font_char_info
{
  unsigned width;
  unsigned data_offset;
};

int main(int argc, char *argv[])
{
  if (argc < 5) {
    fputs("Usage: font_compiler <ttf> <font_name> <height> <monospace_width>\n"
          "\t<ttf> is the TTF file name and <font_name> sets the name of the "
          "generated font.\n"
          "\tThe font is scaled to be <height> pixels tall.\n"
          "\tZero or negative <monospace_width> generates a variable-width font.\n"
          "\tPositive <monospace_width> generates a monospace font with the "
          "specified width.\n",
          stdout);
    return 1;
  }

  uint8_t *ttf_buffer = malloc(1 << 20);
  fread(ttf_buffer, 1, 1 << 20, fopen(argv[1], "rb"));

  const char *font_name = argv[2];
  const size_t font_name_len = strlen(font_name);
  char font_name_upper[font_name_len + 1];
  char font_name_lower[font_name_len + 1];
  for (int i = 0; font_name[i]; ++i) {
    font_name_upper[i] = toupper(font_name[i]);
    font_name_lower[i] = tolower(font_name[i]);
  }
  font_name_upper[font_name_len] = 0;
  font_name_lower[font_name_len] = 0;

  const unsigned height = atoi(argv[3]);
  const int monospace_width = atoi(argv[4]);

  stbtt_fontinfo font;
  stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));

  double scale = stbtt_ScaleForPixelHeight(&font, height);
  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  ascent = (int)round(ascent * scale);
  descent = (int)round(descent * scale);
  unsigned total_height = ascent - descent;

  printf("#define %s_HEIGHT %u\n"
         "static uint8_t %s_data[] = {\n",
         font_name_upper,
         total_height,
         font_name_lower);

  struct font_char_info infos[127 - 32];
  unsigned data_offset = 0;

  for (char c = 32; c < 127; ++c) {
    printf("  // '%c'\n", c);

    int w, h, xoff, yoff;
    uint8_t *bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, c, &w, &h, &xoff, &yoff);
    int padding_top = ascent + yoff;
    int padding_bottom = total_height - (padding_top + h);

    int advance_width, l_bearing;
    stbtt_GetCodepointHMetrics(&font, c, &advance_width, &l_bearing);
    l_bearing = (int)round(l_bearing * scale);
    advance_width = (int)round(advance_width * scale);

    if (l_bearing < 0)
      l_bearing = 0;
    if (l_bearing + w > advance_width)
      advance_width = l_bearing + w;

    int monospace_xoff = 0;
    if (monospace_width > 0) {
      advance_width = monospace_width;
      if (l_bearing + w > advance_width)
        // Clip the left side of the character
        monospace_xoff = l_bearing + w - advance_width;
    }

    int padding_right = advance_width - (l_bearing + w - monospace_xoff);

    infos[c - 32].width = advance_width;
    infos[c - 32].data_offset = data_offset;

    for (int y = 0; y < padding_top; ++y) {
      for (int x = 0; x < advance_width; ++x)
        fputs(" 0,", stdout);
      data_offset += advance_width;
      putchar('\n');
    }

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < l_bearing; ++x)
        fputs(" 0,", stdout);
      for (int x = monospace_xoff; x < w; ++x)
        printf(" %d,", bitmap[y * w + x]);
      for (int x = 0; x < padding_right; ++x)
        fputs(" 0,", stdout);
      data_offset += advance_width;
      putchar('\n');
    }

    for (int y = 0; y < padding_bottom; ++y) {
      for (int x = 0; x < advance_width; ++x)
        fputs(" 0,", stdout);
      data_offset += advance_width;
      putchar('\n');
    }

    free(bitmap);
    putchar('\n');
  }
  fputs("};\n\n", stdout);

  printf("static struct font_char_info %s_char_info[] = {\n", font_name_lower);
  for (char c = 32; c < 127; ++c)
    printf("  // '%c'\n  { .width = %u, .data_offset = %u },\n",
           c,
           infos[c - 32].width,
           infos[c - 32].data_offset);

  fputs("};\n", stdout);

  return 0;
}
