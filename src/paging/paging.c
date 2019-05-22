
// paging.c
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <common/constants.h>
#include "paging.h"

static page_directory_t *kernel_pd = 0;

// Initialize paging.
uint32_t paging_init(page_directory_t *pd)
{
  kernel_pd = pd;

  return 0;
}
