
// qsort.c
//
// (Not very) quick sorting algorithm.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static void swap(void *a, void *b, size_t w)
{
  char tmp[w];
  memcpy(tmp, a, w);
  memcpy(a, b, w);
  memcpy(b, tmp, w);
}

// Always uses the first element as the pivot.
static size_t partition(
  void *base,
  size_t n,
  size_t width,
  int (*cmp)(const void *, const void *)
  )
{
  char *cbase = base;
  size_t pivot_idx = 0;
  for (size_t i = 1; i < n; ++i) {
    char *pivot_elem = cbase + (pivot_idx * width);
    char *i_elem = cbase + (i * width);
    if (cmp(i_elem, pivot_elem) < 0) {
      swap(pivot_elem, pivot_elem + width, width);
      if (i - pivot_idx > 1) swap(pivot_elem, i_elem, width);
      ++pivot_idx;
    }
  }

  return pivot_idx;
}

void qsort(
  void *base,
  size_t n,
  size_t width,
  int (*cmp)(const void *, const void *)
  )
{
  if (n == 0 || width == 0) return;
  char *cbase = base;
  size_t pivot_idx = partition(base, n, width, cmp);
  qsort(base, pivot_idx, width, cmp);
  qsort(cbase + ((pivot_idx + 1) * width), n - pivot_idx - 1, width, cmp);
}
