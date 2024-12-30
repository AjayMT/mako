#!/bin/sh
find . -iname '*.h' -o -iname '*.c' \
  | grep -v ports \
  | grep -v "src/libc/printf.c" \
  | grep -v stb_truetype \
  | xargs clang-format -i

clang-format -i ports/doomgeneric/doomgeneric/doomgeneric_mako.c

find . -name '*.py' | xargs black --line-length 100
