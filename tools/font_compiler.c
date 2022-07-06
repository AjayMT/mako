
// TTF -> C array font compiler using stb_truetype.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Lots of bad hard-coding for LucidaMonoEF.
#define FONTHEIGHT 14
#define FONTWIDTH 8

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("Usage: font_compiler <ttf>\n");
    return 1;
  }

  uint8_t *ttf_buffer = malloc(1 << 20);
  fread(ttf_buffer, 1, 1 << 20, fopen(argv[1], "rb"));

  stbtt_fontinfo font;
  stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));

  double scale = stbtt_ScaleForPixelHeight(&font, FONTHEIGHT + 1);
  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  ascent = (int)(ascent * scale);
  descent = (int)(descent * scale);
  int total_height = ascent - descent;

  fputs("static char font[] = {\n", stdout);
  for (char c = 32; c < 127; ++c) {
    printf("// '%c'\n", c);

    int w, h, xoff, yoff;
    uint8_t *bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, c, &w, &h, &xoff, &yoff);
    int padding_top = ascent + yoff;
    int padding_bottom = total_height - (padding_top + h);

    int advance_width, l_bearing;
    stbtt_GetCodepointHMetrics(&font, c, &advance_width, &l_bearing);
    l_bearing = (int)(l_bearing * scale);
    int padding_right = FONTWIDTH - (l_bearing + w);

    for (int y = 0; y < padding_top; ++y) {
      for (int x = 0; x < FONTWIDTH; ++x) fputs(" 0,", stdout);
      putchar('\n');
    }

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < l_bearing; ++x) fputs(" 0,", stdout);
      for (int x = 0; x < w; ++x) printf(" %d,", bitmap[y * w + x]);
      for (int x = 0; x < padding_right; ++x) fputs(" 0,", stdout);
      putchar('\n');
    }

    for (int y = 0; y < padding_bottom; ++y) {
      for (int x = 0; x < FONTWIDTH; ++x) fputs(" 0,", stdout);
      putchar('\n');
    }

    putchar('\n');
    free(bitmap);
  }
  fputs("};\n", stdout);
  return 0;
}
