
// paging.c
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <common/constants.h>
#include "paging.h"

static page_directory_t *kernel_pd = 0;
static page_table_t *kernel_pt = 0;

// Initialize paging.
void paging_init(page_directory_t *pd, page_table_t *pt)
{
  kernel_pd = pd;
  kernel_pt = pt;
}
