
// PNG -> C array bitmap converter using lodepng.

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "lodepng.h"

int main(int argc, char *argv[])
{
  if (argc < 3) {
    printf("Usage: %s <input-file> <prefix>\n", argv[0]);
    return 1;
  }

  char *input_path = argv[1];
  uint8_t *image = NULL;
  unsigned w = 0;
  unsigned h = 0;
  unsigned err = lodepng_decode32_file(&image, &w, &h, input_path);
  if (err) {
    printf("PNG decode error %u: %s\n", err, lodepng_error_text(err));
    return 1;
  }

  char *prefix = argv[2];
  printf("static const uint32_t %s_WIDTH = %u;\n", prefix, w);
  printf("static const uint32_t %s_HEIGHT = %u;\n", prefix, h);
  printf("static const uint32_t %s_PIXELS[] = {\n", prefix);

  for (unsigned i = 0; i < w * h; ++i) {
    const unsigned pixel_offset = sizeof(uint32_t) * i;
    const uint8_t r = image[pixel_offset];
    const uint8_t g = image[pixel_offset + 1];
    const uint8_t b = image[pixel_offset + 2];
    const uint8_t a = image[pixel_offset + 3];
    const uint32_t output_pixel =
        (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | ((uint32_t)a << 24);
    printf("  0x%x,\n", output_pixel);
  }

  printf("};\n");

  return 0;
}
