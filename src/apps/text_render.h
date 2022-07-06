
// text_render.h
//
// Simple text renderer.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _TEXT_RENDER_H_
#define _TEXT_RENDER_H_

#include <stddef.h>
#include <stdint.h>
#include "font_lucida_mono_ef.h"

void text_dimensions(const char *str, size_t len, size_t *w, size_t *h);
void text_render(const char *str, size_t len, size_t w, size_t h, uint8_t *buf);

#endif /* _TEXT_RENDER_H_ */
