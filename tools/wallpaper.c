
// PNG -> ".wp" wallpaper image converter using lodepng.

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "lodepng.h"

int main(int argc, char *argv[])
{
  if (argc < 3) {
    printf("Usage: wallpaper <input-file> <output-file>\n");
    return 1;
  }

  char *input_path = argv[1];
  char *output_path = argv[2];
  FILE *output = fopen(output_path, "w");
  if (output == NULL) return 1;

  uint8_t *image = NULL;
  unsigned w = 0;
  unsigned h = 0;
  unsigned err = lodepng_decode24_file(&image, &w, &h, input_path);
  if (err) {
    printf("PNG decode error %u: %s\n", err, lodepng_error_text(err));
    return 1;
  }

  static const unsigned expected_w = 1024;
  static const unsigned expected_h = 768;
  if (w != expected_w || h != expected_h) {
    printf("Expected image dimensions 1024*768; got %u*%u\n", w, h);
    return 1;
  }

  for (unsigned i = 0; i < w * h; ++i) {
    const unsigned pixel_offset = 3 * i;
    const uint8_t r = image[pixel_offset];
    const uint8_t g = image[pixel_offset + 1];
    const uint8_t b = image[pixel_offset + 2];
    const uint32_t output_pixel =
        (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | 0xff000000;
    size_t n = fwrite(&output_pixel, sizeof(uint32_t), 1, output);
    if (n != 1) {
      printf("Failed to write output at pixel %u\n", i);
      return 1;
    }
  }

  fclose(output);
  return 0;
}
