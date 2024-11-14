
// wallpaper.c
//
// Change desktop wallpaper.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include <stdio.h>
#include <errno.h>
#include <ui.h>

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("Usage: wallpaper <file>\n");
    return 1;
  }

  int32_t err = ui_set_wallpaper(argv[1]);
  if (err) {
    printf("Failed to set wallpaper: %s\n", strerror(errno));
    return 1;
  }

  return 0;
}
