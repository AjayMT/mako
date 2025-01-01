
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ui.h"
#include "../common/scancode.h"
#include "ds.h"
#include "fs.h"
#include "interrupt.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "paging.h"
#include "pipe.h"
#include "process.h"
#include "ui_cursor.h"
#include "ui_font_data.h"
#include "ui_title_bar.h"
#include "ui_window_shadow.h"
#include "util.h"
#include <stdbool.h>
#include <stdint.h>

#define CHECK(err, msg, code)                                                                      \
  if ((err)) {                                                                                     \
    log_error("ui", msg "\n");                                                                     \
    return (code);                                                                                 \
  }
#define CHECK_UNLOCK_R(err, msg, code)                                                             \
  if ((err)) {                                                                                     \
    log_error("ui", msg "\n");                                                                     \
    kunlock(&responders_lock);                                                                     \
    return (code);                                                                                 \
  }
#define CHECK_RESTORE_EFLAGS(err, msg, code)                                                       \
  if ((err)) {                                                                                     \
    log_error("ui", msg "\n");                                                                     \
    interrupt_restore(eflags);                                                                     \
    return (code);                                                                                 \
  }

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

struct point
{
  int32_t x;
  int32_t y;
};

struct dim
{
  uint32_t w;
  uint32_t h;
};

struct pixel_buffer
{
  uint32_t *buf;
  uint32_t stride;
};

struct responder
{
  process_t *process;
  struct point window_pos;
  struct dim window_dim;
  struct dim resize_dim;
  uint8_t window_opacity;
  bool window_drawn;
  bool window_moving;
  bool window_resizing_w;
  bool window_resizing_h;
  bool mouse_move_events_enabled;
  struct pixel_buffer buf;
  struct pixel_buffer bg_buf;
  struct pixel_buffer title_bar_buf;
  fs_node_t event_pipe_read;
  fs_node_t event_pipe_write;
  list_node_t *list_node;
};

static const uint32_t window_resize_control_size = 5;

static const size_t frame_size = SCREENWIDTH * SCREENHEIGHT;
static uint32_t *wallpaper = NULL;
static struct pixel_buffer window_blit_buffer = { NULL, 0 };
static struct pixel_buffer cursor_bg_buffer = { NULL, 0 };
static struct pixel_buffer back_buffer = { NULL, 0 };
static struct pixel_buffer frame_buffer = { NULL, 0 };

struct point mouse_pos = { 100, 100 };
static uint8_t mouse_left_clicked = 0;
static const uint8_t *cursor_pixels = CURSOR_DEFAULT_PIXELS;
static bool key_lshift_pressed = false;
static bool key_rshift_pressed = false;

static list_t responders;
static volatile uint32_t responders_lock = 0;

static struct responder *responders_by_gid[MAX_PROCESS_COUNT];

static inline uint32_t blend_alpha(uint32_t bg, uint32_t fg, uint8_t opacity)
{
  if (opacity == 0)
    return bg;
  if (opacity == 0xff)
    return fg;

  const uint8_t fg_b = ((fg & 0xff) * opacity) / 0xff;
  const uint8_t fg_g = (((fg >> 8) & 0xff) * opacity) / 0xff;
  const uint8_t fg_r = (((fg >> 16) & 0xff) * opacity) / 0xff;
  const uint32_t bg_b = bg & 0xff;
  const uint32_t bg_g = (bg >> 8) & 0xff;
  const uint32_t bg_r = (bg >> 16) & 0xff;

  const uint16_t t = 0xff ^ opacity;
  const uint32_t blend_g = fg_g + (((bg_g * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_b = fg_b + (((bg_b * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_r = fg_r + (((bg_r * t + 0x80) * 0x101) >> 16);

  return blend_b | (blend_g << 8) | (blend_r << 16);
}

static void copy_rect_alpha(struct pixel_buffer dst,
                            struct point dst_point,
                            const struct pixel_buffer src,
                            struct point src_point,
                            const struct pixel_buffer src_alpha_bg,
                            struct point src_alpha_bg_point,
                            const struct dim dim,
                            uint8_t opacity)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    const uint32_t dst_offset = (dst_point.y + y) * dst.stride + dst_point.x;
    const uint32_t src_offset = (src_point.y + y) * src.stride + src_point.x;
    const uint32_t bg_offset =
      (src_alpha_bg_point.y + y) * src_alpha_bg.stride + src_alpha_bg_point.x;
    if (opacity == 0xff)
      u_memcpy32(dst.buf + dst_offset, src.buf + src_offset, dim.w);
    else
      for (uint32_t x = 0; x < dim.w; ++x)
        dst.buf[dst_offset + x] =
          blend_alpha(src_alpha_bg.buf[bg_offset + x], src.buf[src_offset + x], opacity);
  }
}

static void copy_rect(struct pixel_buffer dst,
                      struct point dst_point,
                      const struct pixel_buffer src,
                      struct point src_point,
                      const struct dim dim)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    const uint32_t dst_offset = (dst_point.y + y) * dst.stride + dst_point.x;
    const uint32_t src_offset = (src_point.y + y) * src.stride + src_point.x;
    u_memcpy32(dst.buf + dst_offset, src.buf + src_offset, dim.w);
  }
}

static void draw_window_shadows(struct dim window_dim, uint8_t window_opacity)
{
  const uint32_t window_shadow_color = 0x5b7c99;
  const uint8_t max_opacity = min(window_opacity, 0x33);

  // Draw shadows around the title bar.
  for (int32_t y = 0; y < WINDOW_SHADOW_SIZE; ++y) {
    uint32_t *blitbuf_ptr = window_blit_buffer.buf + y * window_blit_buffer.stride;

    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[(WINDOW_SHADOW_SIZE - 1 - y) * WINDOW_SHADOW_SIZE +
                                                 (WINDOW_SHADOW_SIZE - 1 - x)];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);
    }

    const uint8_t pixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE - 1 - y];
    const uint8_t opacity = (max_opacity * pixel) / 0xff;
    for (int32_t x = WINDOW_SHADOW_SIZE; x < WINDOW_SHADOW_SIZE + TITLE_BAR_WIDTH; ++x)
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);

    const int32_t xoff = WINDOW_SHADOW_SIZE + TITLE_BAR_WIDTH;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel =
        WINDOW_SHADOW_PIXELS[(WINDOW_SHADOW_SIZE - 1 - y) * WINDOW_SHADOW_SIZE + x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }
  for (int32_t y = WINDOW_SHADOW_SIZE; y < WINDOW_SHADOW_SIZE + TITLE_BAR_HEIGHT; ++y) {
    uint32_t *blitbuf_ptr = window_blit_buffer.buf + y * window_blit_buffer.stride;

    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE - 1 - x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);
    }
  }
  const int32_t y_concave_corner = TITLE_BAR_HEIGHT;
  for (int32_t y = WINDOW_SHADOW_SIZE; y < y_concave_corner; ++y) {
    uint32_t *blitbuf_ptr = window_blit_buffer.buf + y * window_blit_buffer.stride;

    const int32_t xoff = WINDOW_SHADOW_SIZE + TITLE_BAR_WIDTH;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }
  for (int32_t y = 0; y < WINDOW_SHADOW_SIZE; ++y) {
    uint32_t *blitbuf_ptr =
      window_blit_buffer.buf + (y + y_concave_corner) * window_blit_buffer.stride;

    const uint8_t ypixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE - 1 - y];
    const int32_t xoff = WINDOW_SHADOW_SIZE + TITLE_BAR_WIDTH;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t xpixel = WINDOW_SHADOW_PIXELS[x];
      const uint8_t pixel = max(xpixel, ypixel);
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }

  // Draw shadows around the window.
  for (int32_t y = 0; y < (int32_t)window_dim.h; ++y) {
    uint32_t *blitbuf_ptr = window_blit_buffer.buf +
                            (y + WINDOW_SHADOW_SIZE + TITLE_BAR_HEIGHT) * window_blit_buffer.stride;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE - 1 - x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);
    }
    const int32_t xoff = WINDOW_SHADOW_SIZE + window_dim.w;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }
  const int32_t x_concave_corner = WINDOW_SHADOW_SIZE * 2 + TITLE_BAR_WIDTH;
  for (int32_t y = 0; y < WINDOW_SHADOW_SIZE; ++y) {
    uint32_t *blitbuf_ptr =
      window_blit_buffer.buf + (y + y_concave_corner) * window_blit_buffer.stride;

    const uint8_t pixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE - 1 - y];
    const uint8_t opacity = (max_opacity * pixel) / 0xff;
    for (int32_t x = x_concave_corner; x < WINDOW_SHADOW_SIZE + (int32_t)window_dim.w; ++x)
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);

    const int32_t xoff = WINDOW_SHADOW_SIZE + window_dim.w;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel =
        WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE * (WINDOW_SHADOW_SIZE - 1 - y) + x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }
  for (int32_t y = 0; y < WINDOW_SHADOW_SIZE; ++y) {
    uint32_t *blitbuf_ptr =
      window_blit_buffer.buf +
      (y + WINDOW_SHADOW_SIZE + TITLE_BAR_HEIGHT + window_dim.h) * window_blit_buffer.stride;

    const uint8_t pixel = WINDOW_SHADOW_PIXELS[y];
    const uint8_t opacity = (max_opacity * pixel) / 0xff;
    for (int32_t x = WINDOW_SHADOW_SIZE; x < WINDOW_SHADOW_SIZE + (int32_t)window_dim.w; ++x)
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);

    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel =
        WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE * y + (WINDOW_SHADOW_SIZE - 1 - x)];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x] = blend_alpha(blitbuf_ptr[x], window_shadow_color, opacity);
    }
    const int32_t xoff = WINDOW_SHADOW_SIZE + window_dim.w;
    for (int32_t x = 0; x < WINDOW_SHADOW_SIZE; ++x) {
      const uint8_t pixel = WINDOW_SHADOW_PIXELS[WINDOW_SHADOW_SIZE * y + x];
      const uint8_t opacity = (max_opacity * pixel) / 0xff;
      blitbuf_ptr[x + xoff] = blend_alpha(blitbuf_ptr[x + xoff], window_shadow_color, opacity);
    }
  }
}

static void invert_rect(struct pixel_buffer buf, struct point pos, struct dim dim)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    uint32_t offset = (pos.y + y) * buf.stride + pos.x;
    for (uint32_t x = 0; x < dim.w; ++x)
      buf.buf[offset + x] = ~buf.buf[offset + x];
  }
}

static bool clip_rect_on_screen(struct point *dst, struct point *src, struct dim *dim)
{
  if (dst->x + (int32_t)dim->w < 0 || dst->x >= SCREENWIDTH || dst->y + (int32_t)dim->h < 0 ||
      dst->y >= SCREENHEIGHT)
    return false;

  const struct point clipped_dst = {
    .x = max(dst->x, 0),
    .y = max(dst->y, 0),
  };
  src->x += clipped_dst.x - dst->x;
  src->y += clipped_dst.y - dst->y;
  dim->w = min(dst->x + dim->w, SCREENWIDTH) - clipped_dst.x;
  dim->h = min(dst->y + dim->h, SCREENHEIGHT) - clipped_dst.y;
  dst->x = clipped_dst.x;
  dst->y = clipped_dst.y;
  return true;
}

static inline bool mouse_in_rect(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
  return mouse_pos.x >= x && mouse_pos.x < x + (int32_t)w && mouse_pos.y >= y &&
         mouse_pos.y < y + (int32_t)h;
}

static inline bool rect_intersect(struct point p1, struct dim d1, struct point p2, struct dim d2)
{
  const bool x_int =
    (p1.x >= p2.x && p1.x < p2.x + (int32_t)d2.w) || (p2.x >= p1.x && p2.x < p1.x + (int32_t)d1.w);
  const bool y_int =
    (p1.y >= p2.y && p1.y < p2.y + (int32_t)d2.h) || (p2.y >= p1.y && p2.y < p1.y + (int32_t)d1.h);
  return x_int && y_int;
}

static void render_char(struct pixel_buffer buf,
                        struct point dst,
                        struct font_char_info c,
                        const uint8_t *font_data,
                        unsigned font_height)
{
  uint32_t *p = buf.buf + dst.y * buf.stride + dst.x;
  for (size_t y = 0; y < font_height; ++y) {
    for (size_t x = 0; x < c.width; ++x) {
      uint8_t opacity = font_data[c.data_offset + y * c.width + x];
      p[y * buf.stride + x] = blend_alpha(p[y * buf.stride + x], 0, opacity);
    }
  }
}

static void render_text(struct pixel_buffer buf, struct point dst, const char *str)
{
  const struct font_char_info *font_char_info = lucida_grande_char_info;
  const uint8_t *font_data = lucida_grande_data;
  const unsigned font_height = LUCIDA_GRANDE_HEIGHT;

  int32_t x = dst.x;
  int32_t y = dst.y;
  for (size_t i = 0; str[i]; ++i) {
    if (x >= (int32_t)buf.stride)
      break;

    char c = 0;
    switch (str[i]) {
      case '\n':
        y += font_height;
        x = 0;
        break;
      case '\t':
        x += 4 * font_char_info[' ' - 32].width;
        break;
      default:
        c = str[i];
    }

    if (c < 32 || c > 126)
      continue;

    struct point char_dst = { x, y };
    const struct font_char_info char_info = font_char_info[c - 32];
    render_char(buf, char_dst, char_info, font_data, font_height);
    x += char_info.width;
  }
}

static inline struct point window_chrome_pos(struct point window_pos)
{
  return (struct point){ window_pos.x - WINDOW_SHADOW_SIZE,
                         window_pos.y - TITLE_BAR_HEIGHT - WINDOW_SHADOW_SIZE };
}
static inline struct dim window_chrome_dim(struct dim window_dim)
{
  return (struct dim){ window_dim.w + WINDOW_SHADOW_SIZE * 2,
                       window_dim.h + TITLE_BAR_HEIGHT + WINDOW_SHADOW_SIZE * 2 };
}
static inline struct point window_chrome_content_offset(struct point chrome_pos)
{
  return (struct point){ chrome_pos.x + WINDOW_SHADOW_SIZE,
                         chrome_pos.y + TITLE_BAR_HEIGHT + WINDOW_SHADOW_SIZE };
}

static void blit_window(struct responder *r)
{
  const process_t *p = r->process;
  const uint32_t eflags = interrupt_save_disable();
  const uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  struct point bg_buf_dst = window_chrome_pos(r->window_pos);
  struct point bg_buf_src = { 0, 0 };
  struct dim bg_buf_dim = window_chrome_dim(r->window_dim);
  if (!clip_rect_on_screen(&bg_buf_dst, &bg_buf_src, &bg_buf_dim))
    return;

  // Copy the contents of the screen into the window background buffer.
  copy_rect(r->bg_buf, bg_buf_src, back_buffer, bg_buf_dst, bg_buf_dim);

  // Draw the background, window chrome and window contents onto the window
  // blit buffer.
  copy_rect(window_blit_buffer,
            (struct point){ 0, 0 },
            r->bg_buf,
            (struct point){ 0, 0 },
            window_chrome_dim(r->window_dim));
  draw_window_shadows(r->window_dim, r->window_opacity);
  struct point title_bar_dst = { WINDOW_SHADOW_SIZE, WINDOW_SHADOW_SIZE };
  copy_rect_alpha(window_blit_buffer,
                  title_bar_dst,
                  r->title_bar_buf,
                  (struct point){ 0, 0 },
                  r->bg_buf,
                  title_bar_dst,
                  (struct dim){ TITLE_BAR_WIDTH, TITLE_BAR_HEIGHT },
                  r->window_opacity);
  struct point window_dst = window_chrome_content_offset((struct point){ 0, 0 });
  copy_rect_alpha(window_blit_buffer,
                  window_dst,
                  r->buf,
                  (struct point){ 0, 0 },
                  r->bg_buf,
                  window_dst,
                  r->window_dim,
                  r->window_opacity);

  // Copy the window blit buffer onto the backbuffer.
  copy_rect(back_buffer, bg_buf_dst, window_blit_buffer, bg_buf_src, bg_buf_dim);

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
}

static void clear_cursor()
{
  struct point cursor_dst = mouse_pos;
  struct dim cursor_dim = { CURSOR_WIDTH, CURSOR_HEIGHT };
  struct point cursor_src = { 0, 0 };
  if (clip_rect_on_screen(&cursor_dst, &cursor_src, &cursor_dim))
    copy_rect(back_buffer, cursor_dst, cursor_bg_buffer, cursor_src, cursor_dim);
}

static void blit_cursor()
{
  uint32_t cursor_w = min(mouse_pos.x + CURSOR_WIDTH, SCREENWIDTH) - mouse_pos.x;
  uint32_t cursor_h = min(mouse_pos.y + CURSOR_HEIGHT, SCREENHEIGHT) - mouse_pos.y;
  const uint32_t cursor_colors[] = { 0, 0xffffff };

  for (uint32_t y = 0; y < cursor_h; ++y) {
    const uint32_t row_offset = (mouse_pos.y + y) * SCREENWIDTH;
    for (uint32_t x = 0; x < cursor_w; ++x) {
      const uint32_t pixel_offset = row_offset + mouse_pos.x + x;
      cursor_bg_buffer.buf[y * CURSOR_WIDTH + x] = back_buffer.buf[pixel_offset];
      uint8_t cursor_pixel = cursor_pixels[y * CURSOR_WIDTH + x];
      if (cursor_pixel == CURSOR_NONE)
        continue;
      back_buffer.buf[pixel_offset] = cursor_colors[cursor_pixel];
    }
  }
}

static void redraw_key_responder(struct point origin, struct dim dim)
{
  uint32_t eflags = interrupt_save_disable();
  struct responder *r = responders.head->value;

  if ((uint32_t)origin.x >= r->window_dim.w || (uint32_t)origin.y >= r->window_dim.h) {
    interrupt_restore(eflags);
    return;
  }

  struct point invert_dst = r->window_pos;
  struct point invert_src = { 0, 0 };
  struct dim invert_dim = r->resize_dim;
  if (r->window_resizing_w || r->window_resizing_h) {
    if (clip_rect_on_screen(&invert_dst, &invert_src, &invert_dim))
      invert_rect(back_buffer, invert_dst, invert_dim);
  }

  struct point src = origin;
  struct point dst = { r->window_pos.x + origin.x, r->window_pos.y + origin.y };
  struct dim clipped_dim = {
    .w = min(origin.x + dim.w, r->window_dim.w) - origin.x,
    .h = min(origin.y + dim.h, r->window_dim.h) - origin.y,
  };

  if (!clip_rect_on_screen(&dst, &src, &clipped_dim)) {
    interrupt_restore(eflags);
    return;
  }

  bool redraw_cursor =
    rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, dst, clipped_dim);
  if (redraw_cursor)
    clear_cursor();

  const uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(r->process->cr3);

  copy_rect_alpha(back_buffer,
                  dst,
                  r->buf,
                  src,
                  r->bg_buf,
                  window_chrome_content_offset(src),
                  clipped_dim,
                  r->window_opacity);
  paging_set_cr3(cr3);

  if (redraw_cursor)
    blit_cursor();

  if (r->window_resizing_w || r->window_resizing_h)
    invert_rect(back_buffer, invert_dst, invert_dim);

  copy_rect(frame_buffer, dst, back_buffer, dst, clipped_dim);

  interrupt_restore(eflags);
  return;
}

static void redraw_moving_objects(struct point old, struct point new)
{
  uint32_t moved_object_w = CURSOR_WIDTH;
  uint32_t moved_object_h = CURSOR_HEIGHT;
  bool window_moving = false;

  struct responder *key_responder = NULL;
  if (responders.size) {
    key_responder = responders.head->value;
    if (key_responder->window_moving) {
      struct dim moved_object_dim = window_chrome_dim(key_responder->window_dim);
      moved_object_w = moved_object_dim.w, moved_object_h = moved_object_dim.h;
      window_moving = true;
    }
  }

  struct point old_dst = old;
  struct point old_src = { 0, 0 };
  struct dim old_dim = { moved_object_w, moved_object_h };

  if (clip_rect_on_screen(&old_dst, &old_src, &old_dim)) {
    if (window_moving)
      copy_rect(back_buffer, old_dst, key_responder->bg_buf, old_src, old_dim);
    else
      copy_rect(back_buffer, old_dst, cursor_bg_buffer, old_src, old_dim);
  }

  if (window_moving)
    blit_window(key_responder);

  blit_cursor();

  const int32_t min_x = min(old.x, new.x);
  const int32_t max_x = max(old.x, new.x);
  const int32_t min_y = min(old.y, new.y);
  const int32_t max_y = max(old.y, new.y);

  struct point redraw_dst = { min_x, min_y };
  struct point redraw_src = { 0, 0 };
  struct dim redraw_dim = {
    .w = max_x + moved_object_w - min_x,
    .h = max_y + moved_object_h - min_y,
  };
  if (clip_rect_on_screen(&redraw_dst, &redraw_src, &redraw_dim))
    copy_rect(frame_buffer, redraw_dst, back_buffer, redraw_dst, redraw_dim);
}

static void redraw_resizing_window(struct point old_mouse_pos,
                                   struct point window_pos,
                                   struct dim old_resize_dim,
                                   struct dim new_resize_dim)
{
  struct point old_invert_dst = window_pos;
  struct point old_invert_src = { 0, 0 };
  struct dim old_invert_dim = old_resize_dim;
  if (clip_rect_on_screen(&old_invert_dst, &old_invert_src, &old_invert_dim))
    invert_rect(back_buffer, old_invert_dst, old_invert_dim);

  struct point old_mouse_dst = old_mouse_pos;
  struct point old_mouse_src = { 0, 0 };
  struct dim mouse_dim = { CURSOR_WIDTH, CURSOR_HEIGHT };
  if (clip_rect_on_screen(&old_mouse_dst, &old_mouse_src, &mouse_dim))
    copy_rect(back_buffer, old_mouse_dst, cursor_bg_buffer, old_mouse_src, mouse_dim);

  blit_cursor();

  struct point new_invert_dst = window_pos;
  struct point new_invert_src = { 0, 0 };
  struct dim new_invert_dim = new_resize_dim;
  if (clip_rect_on_screen(&new_invert_dst, &new_invert_src, &new_invert_dim))
    invert_rect(back_buffer, new_invert_dst, new_invert_dim);

  int32_t max_x = max(old_mouse_pos.x, mouse_pos.x) + CURSOR_WIDTH;
  max_x = max(max_x, window_pos.x + (int32_t)old_resize_dim.w);
  max_x = max(max_x, window_pos.x + (int32_t)new_resize_dim.w);
  int32_t max_y = max(old_mouse_pos.y, mouse_pos.y) + CURSOR_HEIGHT;
  max_y = max(max_y, window_pos.y + (int32_t)old_resize_dim.h);
  max_y = max(max_y, window_pos.y + (int32_t)new_resize_dim.h);

  struct point redraw_dst = window_pos;
  struct point redraw_src = { 0, 0 };
  struct dim redraw_dim = { (uint32_t)(max_x - window_pos.x), (uint32_t)(max_y - window_pos.y) };
  if (clip_rect_on_screen(&redraw_dst, &redraw_src, &redraw_dim))
    copy_rect(frame_buffer, redraw_dst, back_buffer, redraw_dst, redraw_dim);
}

static void redraw_all()
{
  const uint32_t eflags = interrupt_save_disable();

  u_memcpy32(back_buffer.buf, wallpaper, frame_size);

  for (list_node_t *current = responders.tail; current != NULL; current = current->prev) {
    struct responder *responder = current->value;
    blit_window(responder);
  }

  blit_cursor();
  u_memcpy32(frame_buffer.buf, back_buffer.buf, frame_size);

  interrupt_restore(eflags);
}

uint32_t ui_init(uint32_t video_vaddr)
{
  u_memset(responders_by_gid, 0, sizeof(responders_by_gid));
  u_memset(&responders, 0, sizeof(list_t));
  frame_buffer.buf = (uint32_t *)video_vaddr;
  frame_buffer.stride = SCREENWIDTH;

  back_buffer.buf = kmalloc(frame_size * sizeof(uint32_t));
  CHECK(back_buffer.buf == NULL, "Failed to allocate back_buffer", ENOMEM);
  back_buffer.stride = SCREENWIDTH;

  window_blit_buffer.buf = kmalloc(frame_size * sizeof(uint32_t));
  CHECK(window_blit_buffer.buf == NULL, "Failed to allocate window_blit_buffer", ENOMEM);
  window_blit_buffer.stride = SCREENWIDTH;

  wallpaper = kmalloc(frame_size * sizeof(uint32_t));
  CHECK(wallpaper == NULL, "Failed to allocate wallpaper", ENOMEM);

  cursor_bg_buffer.buf = kmalloc(CURSOR_WIDTH * CURSOR_HEIGHT * sizeof(uint32_t));
  CHECK(cursor_bg_buffer.buf == NULL, "Failed to allocate cursor_bg_buffer", ENOMEM);
  cursor_bg_buffer.stride = CURSOR_WIDTH;

  fs_node_t wallpaper_dir;
  uint32_t err = fs_open_node(&wallpaper_dir, "/wallpapers", O_DIRECTORY);
  CHECK(err, "Failed to open /wallpapers", ENOENT);

  struct dirent *wallpaper_dirent = fs_readdir(&wallpaper_dir, 2);
  CHECK(wallpaper_dirent == NULL, "Failed to read entry in /wallpapers", ENOTDIR);

  const size_t path_strlen = u_strlen("/wallpapers/");
  const size_t dirent_strlen = u_strlen(wallpaper_dirent->name);
  char name_buf[path_strlen + dirent_strlen + 1];
  u_memcpy(name_buf, "/wallpapers/", path_strlen);
  u_memcpy(name_buf + path_strlen, wallpaper_dirent->name, dirent_strlen + 1);
  err = ui_set_wallpaper(name_buf);
  CHECK(err, "Failed to set wallpaper", err);
  kfree(wallpaper_dirent);

  return 0;
}

static uint32_t dispatch_window_event(struct responder *r, ui_event_type_t t)
{
  ui_event_t ev;
  ev.type = t;
  if (ev.type == UI_EVENT_RESIZE_REQUEST) {
    ev.width = r->resize_dim.w;
    ev.height = r->resize_dim.h;
  } else {
    ev.width = r->window_dim.w;
    ev.height = r->window_dim.h;
  }

  uint32_t written = fs_write(&r->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);

  if (written != sizeof(ui_event_t))
    return -1;
  return 0;
}

uint32_t ui_handle_keyboard_event(uint8_t code)
{
  uint32_t eflags = interrupt_save_disable();

  if (responders.size == 0) {
    interrupt_restore(eflags);
    return 0;
  }

  static bool meta_pressed = false;

  if (meta_pressed && code == KB_SC_TAB) {
    // Rotate responders list
    if (responders.size > 1) {
      struct responder *key_responder = responders.head->value;
      list_remove(&responders, responders.head, 0);
      key_responder->list_node->prev = responders.tail;
      key_responder->list_node->next = NULL;
      responders.tail->next = key_responder->list_node;
      responders.tail = key_responder->list_node;
      responders.size++;

      uint32_t err = dispatch_window_event(key_responder, UI_EVENT_SLEEP);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch sleep event.", err);
      err = dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch wake event.", err);

      redraw_all();
    }

    interrupt_restore(eflags);
    return 0;
  }

  bool pressed = true;
  uint8_t pressed_code = code;
  if (code & KB_KEY_RELEASED_MASK) {
    pressed_code &= ~KB_KEY_RELEASED_MASK;
    pressed = false;
  }

  switch (pressed_code) {
    case KB_SC_META:
      meta_pressed = pressed;
      break;
    case KB_SC_LSHIFT:
      key_lshift_pressed = pressed;
      break;
    case KB_SC_RSHIFT:
      key_rshift_pressed = pressed;
      break;
  }

  ui_event_t ev;
  ev.type = UI_EVENT_KEYBOARD;
  ev.code = code;

  struct responder *key_responder = responders.head->value;
  uint32_t written =
    fs_write(&key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);

  interrupt_restore(eflags);
  if (written != sizeof(ui_event_t))
    return -1;
  return 0;
}

static void handle_mouse_click()
{
  struct responder *new_key_responder = NULL;
  bool changed_opacity = false;
  list_foreach(node, &responders)
  {
    struct responder *r = node->value;
    if (mouse_in_rect(r->window_pos.x,
                      r->window_pos.y - TITLE_BAR_HEIGHT,
                      TITLE_BAR_BUTTON_WIDTH,
                      TITLE_BAR_HEIGHT)) {
      if (!responders_lock)
        process_kill(r->process);
      return;
    }
    if (mouse_in_rect(r->window_pos.x + TITLE_BAR_WIDTH - TITLE_BAR_BUTTON_WIDTH,
                      r->window_pos.y - TITLE_BAR_HEIGHT,
                      TITLE_BAR_BUTTON_WIDTH,
                      TITLE_BAR_HEIGHT)) {
      new_key_responder = r;
      if (r->window_opacity == 0x99)
        r->window_opacity = 0xff;
      else
        r->window_opacity -= 0x22;
      changed_opacity = true;
      break;
    }
    if (mouse_in_rect(
          r->window_pos.x, r->window_pos.y - TITLE_BAR_HEIGHT, TITLE_BAR_WIDTH, TITLE_BAR_HEIGHT)) {
      new_key_responder = r;
      r->window_moving = true;
      break;
    }

    if (mouse_in_rect(r->window_pos.x, r->window_pos.y, r->window_dim.w, r->window_dim.h)) {
      new_key_responder = r;
      break;
    }

    if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                      r->window_pos.y + r->window_dim.h,
                      window_resize_control_size,
                      window_resize_control_size)) {
      new_key_responder = r;
      r->window_resizing_w = true;
      r->window_resizing_h = true;
      break;
    }
    if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                      r->window_pos.y + window_resize_control_size,
                      window_resize_control_size,
                      r->window_dim.h - window_resize_control_size)) {
      new_key_responder = r;
      r->window_resizing_w = true;
      break;
    }
    if (mouse_in_rect(r->window_pos.x + window_resize_control_size,
                      r->window_pos.y + r->window_dim.h,
                      r->window_dim.w - window_resize_control_size,
                      window_resize_control_size)) {
      new_key_responder = r;
      r->window_resizing_h = true;
      break;
    }
  }

  if (new_key_responder == NULL)
    return;

  struct responder *old_key_responder = responders.head->value;
  const bool changed_key_responder = old_key_responder != new_key_responder;

  if (changed_opacity && !changed_key_responder) {
    struct point dst = window_chrome_pos(new_key_responder->window_pos);
    struct point src = { 0, 0 };
    struct dim dim = window_chrome_dim(new_key_responder->window_dim);
    if (clip_rect_on_screen(&dst, &src, &dim)) {
      bool redraw_cursor =
        rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, dst, dim);
      if (redraw_cursor)
        clear_cursor();

      copy_rect(back_buffer, dst, new_key_responder->bg_buf, src, dim);
      blit_window(new_key_responder);

      if (redraw_cursor)
        blit_cursor();

      copy_rect(frame_buffer, dst, back_buffer, dst, dim);
    }

    return;
  }

  if (changed_key_responder) {
    list_remove(&responders, new_key_responder->list_node, 0);
    new_key_responder->list_node->next = responders.head;
    new_key_responder->list_node->prev = NULL;
    responders.head->prev = new_key_responder->list_node;
    responders.head = new_key_responder->list_node;
    responders.size++;
    uint32_t err = dispatch_window_event(old_key_responder, UI_EVENT_SLEEP);
    if (err)
      log_error("ui", "Failed to dispatch sleep event: %u\n", err);
    err = dispatch_window_event(new_key_responder, UI_EVENT_WAKE);
    if (err)
      log_error("ui", "Failed to dispatch wake event: %u\n", err);
  }

  if (new_key_responder->window_resizing_w || new_key_responder->window_resizing_h) {
    struct point invert_dst = new_key_responder->window_pos;
    struct point invert_src = { 0, 0 };
    struct dim invert_dim = new_key_responder->window_dim;
    if (clip_rect_on_screen(&invert_dst, &invert_src, &invert_dim)) {
      invert_rect(back_buffer, invert_dst, invert_dim);
      if (!changed_key_responder)
        copy_rect(frame_buffer, invert_dst, back_buffer, invert_dst, invert_dim);
    }
  }

  if (changed_key_responder)
    redraw_all();

  bool dispatch_click_event = !changed_key_responder && !new_key_responder->window_moving &&
                              !new_key_responder->window_resizing_w &&
                              !new_key_responder->window_resizing_h;
  if (dispatch_click_event) {
    int32_t click_x = mouse_pos.x - new_key_responder->window_pos.x;
    int32_t click_y = mouse_pos.y - new_key_responder->window_pos.y;
    ui_event_t ev;
    ev.type = UI_EVENT_MOUSE_CLICK;
    ev.x = click_x;
    ev.y = click_y;
    uint32_t written =
      fs_write(&new_key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);
    if (written != sizeof(ui_event_t))
      log_error("ui", "Failed to dispatch click event.");
  }
}

static void handle_mouse_scroll(int8_t vscroll, int8_t hscroll)
{
  list_foreach(node, &responders)
  {
    struct responder *r = node->value;
    if (mouse_in_rect(r->window_pos.x, r->window_pos.y, r->window_dim.w, r->window_dim.h)) {
      ui_event_t ev;
      ev.type = UI_EVENT_MOUSE_SCROLL;
      if (key_lshift_pressed || key_rshift_pressed) {
        ev.vscroll = hscroll;
        ev.hscroll = vscroll;
      } else {
        ev.vscroll = vscroll;
        ev.hscroll = hscroll;
      }
      uint32_t written = fs_write(&r->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);
      if (written != sizeof(ui_event_t))
        log_error("ui", "Failed to dispatch scroll event.");
      return;
    }
  }
}

static void update_mouse_cursor()
{
  cursor_pixels = CURSOR_DEFAULT_PIXELS;

  if (responders.size == 0)
    return;

  struct responder *r = responders.head->value;
  if (mouse_in_rect(r->window_pos.x,
                    r->window_pos.y - TITLE_BAR_HEIGHT,
                    TITLE_BAR_BUTTON_WIDTH,
                    TITLE_BAR_HEIGHT)) {
    cursor_pixels = CURSOR_CLOSE_PIXELS;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + TITLE_BAR_WIDTH - TITLE_BAR_BUTTON_WIDTH,
                    r->window_pos.y - TITLE_BAR_HEIGHT,
                    TITLE_BAR_BUTTON_WIDTH,
                    TITLE_BAR_HEIGHT)) {
    cursor_pixels = CURSOR_OPACITY_PIXELS;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                    r->window_pos.y + r->window_dim.h,
                    window_resize_control_size,
                    window_resize_control_size)) {
    cursor_pixels = CURSOR_RESIZE_WH_PIXELS;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                    r->window_pos.y + window_resize_control_size,
                    window_resize_control_size,
                    r->window_dim.h - window_resize_control_size)) {
    cursor_pixels = CURSOR_RESIZE_W_PIXELS;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + window_resize_control_size,
                    r->window_pos.y + r->window_dim.h,
                    r->window_dim.w - window_resize_control_size,
                    window_resize_control_size)) {
    cursor_pixels = CURSOR_RESIZE_H_PIXELS;
    return;
  }
}

uint32_t ui_handle_mouse_event(int32_t dx,
                               int32_t dy,
                               uint8_t left_button,
                               uint8_t right_button,
                               int8_t vscroll,
                               int8_t hscroll)
{
  uint8_t click_event = mouse_left_clicked != left_button;
  if (dx == 0 && dy == 0 && vscroll == 0 && hscroll == 0 && !click_event)
    return 0;

  mouse_left_clicked = left_button;

  struct responder *key_responder = NULL;
  if (responders.size)
    key_responder = responders.head->value;

  if (vscroll || hscroll)
    handle_mouse_scroll(vscroll, hscroll);

  if (click_event) {
    if (mouse_left_clicked)
      handle_mouse_click();
    else if (key_responder) {
      if (key_responder->window_resizing_w || key_responder->window_resizing_h) {
        key_responder->window_resizing_w = false;
        key_responder->window_resizing_h = false;
        struct point invert_dst = key_responder->window_pos;
        struct point invert_src = { 0, 0 };
        struct dim invert_dim = key_responder->resize_dim;
        if (clip_rect_on_screen(&invert_dst, &invert_src, &invert_dim)) {
          invert_rect(back_buffer, invert_dst, invert_dim);
          copy_rect(frame_buffer, invert_dst, back_buffer, invert_dst, invert_dim);
        }
        dispatch_window_event(key_responder, UI_EVENT_RESIZE_REQUEST);
        key_responder->resize_dim = key_responder->window_dim;
      } else if (!key_responder->window_moving) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_UNCLICK;
        ev.x = mouse_pos.x - key_responder->window_pos.x;
        ev.y = mouse_pos.y - key_responder->window_pos.y;
        uint32_t written =
          fs_write(&key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);
        if (written != sizeof(ui_event_t))
          log_error("ui", "Failed to dispatch unclick event.");
      }
      key_responder->window_moving = false;
    }
    return 0;
  }

  const struct point old_mouse_pos = mouse_pos;

  mouse_pos.x += dx;
  mouse_pos.y -= dy;
  mouse_pos.x = max(mouse_pos.x, 0);
  mouse_pos.x = min(mouse_pos.x, SCREENWIDTH - 1);
  mouse_pos.y = max(mouse_pos.y, 0);
  mouse_pos.y = min(mouse_pos.y, SCREENHEIGHT - 1);

  update_mouse_cursor();

  if (key_responder) {
    if (key_responder->window_moving) {
      struct point old = window_chrome_pos(key_responder->window_pos);
      key_responder->window_pos.x += mouse_pos.x - old_mouse_pos.x;
      key_responder->window_pos.y += mouse_pos.y - old_mouse_pos.y;
      struct point new = window_chrome_pos(key_responder->window_pos);
      redraw_moving_objects(old, new);
      return 0;
    } else if (key_responder->window_resizing_w || key_responder->window_resizing_h) {
      const struct dim old_resize_dim = key_responder->resize_dim;
      const struct dim window_chrome_dims = window_chrome_dim((struct dim){ 0, 0 });
      if (key_responder->window_resizing_w) {
        key_responder->resize_dim.w += mouse_pos.x - old_mouse_pos.x;
        key_responder->resize_dim.w = min(max(key_responder->resize_dim.w, TITLE_BAR_WIDTH),
                                          SCREENWIDTH - window_chrome_dims.w);
      }
      if (key_responder->window_resizing_h) {
        key_responder->resize_dim.h += mouse_pos.y - old_mouse_pos.y;
        key_responder->resize_dim.h =
          min(max(key_responder->resize_dim.h, 50), SCREENHEIGHT - window_chrome_dims.h);
      }
      redraw_resizing_window(
        old_mouse_pos, key_responder->window_pos, old_resize_dim, key_responder->resize_dim);
      return 0;
    } else if (key_responder->mouse_move_events_enabled) {
      ui_event_t ev;
      ev.type = UI_EVENT_MOUSE_MOVE;
      ev.x = mouse_pos.x - key_responder->window_pos.x;
      ev.y = mouse_pos.y - key_responder->window_pos.y;
      ev.dx = dx;
      ev.dy = dy;
      uint32_t written =
        fs_write(&key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);
      if (written != sizeof(ui_event_t))
        log_error("ui", "Failed to dispatch move event.");
    }
  }

  redraw_moving_objects(old_mouse_pos, mouse_pos);

  return 0;
}

uint32_t ui_make_responder(process_t *p, uint32_t buf, const char *title, uint32_t w, uint32_t h)
{
  klock(&responders_lock);
  CHECK_UNLOCK_R(responders_by_gid[p->gid] != NULL, "Process already has window.", 1);

  struct responder *r = kmalloc(sizeof(struct responder));
  CHECK_UNLOCK_R(r == NULL, "No memory.", ENOMEM);
  u_memset(r, 0, sizeof(struct responder));

  uint32_t err = pipe_create(&r->event_pipe_read, &r->event_pipe_write);
  CHECK_UNLOCK_R(err, "Failed to create event pipe.", err);

  const struct dim window_chrome_dims = window_chrome_dim((struct dim){ 0, 0 });
  r->process = p;
  r->window_pos.x = SCREENWIDTH >> 2;
  r->window_pos.y = SCREENHEIGHT >> 2;
  r->window_dim.w = min(max(w, TITLE_BAR_WIDTH), SCREENWIDTH - window_chrome_dims.w);
  r->window_dim.h = min(max(h, 50), SCREENHEIGHT - window_chrome_dims.h);
  r->resize_dim = r->window_dim;
  r->window_opacity = 0xff;
  r->window_drawn = false;
  r->buf = (struct pixel_buffer){ (uint32_t *)buf, r->window_dim.w };

  const struct dim bg_buf_dim = window_chrome_dim(r->window_dim);
  const uint32_t bg_buf_w = bg_buf_dim.w;
  const uint32_t bg_buf_h = bg_buf_dim.h;
  r->bg_buf.buf = kmalloc(bg_buf_w * bg_buf_h * sizeof(uint32_t));
  CHECK_UNLOCK_R(r->bg_buf.buf == NULL, "No memory.", ENOMEM);
  r->bg_buf.stride = bg_buf_dim.w;

  r->title_bar_buf.buf = kmalloc(TITLE_BAR_WIDTH * TITLE_BAR_HEIGHT * sizeof(uint32_t));
  CHECK_UNLOCK_R(r->title_bar_buf.buf == NULL, "No memory.", ENOMEM);
  r->title_bar_buf.stride = TITLE_BAR_WIDTH;
  u_memcpy32(r->title_bar_buf.buf, TITLE_BAR_PIXELS, TITLE_BAR_WIDTH * TITLE_BAR_HEIGHT);
  const struct point title_dst = { TITLE_BAR_BUTTON_WIDTH + 4, 2 };
  render_text(r->title_bar_buf, title_dst, title);

  list_push_front(&responders, r);
  r->list_node = responders.head;
  responders_by_gid[p->gid] = r;
  p->has_ui = 1;
  err = dispatch_window_event(r, UI_EVENT_WAKE);
  CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  if (responders.head->next) {
    err = dispatch_window_event(responders.head->next->value, UI_EVENT_SLEEP);
    CHECK_UNLOCK_R(err, "Failed to dispatch sleep event.", err);
  }
  kunlock(&responders_lock);
  return 0;
}

uint32_t ui_kill(process_t *p)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  if (r == NULL) {
    kunlock(&responders_lock);
    return 0;
  }

  responders_by_gid[p->gid] = NULL;
  fs_close(&r->event_pipe_read);
  fs_close(&r->event_pipe_write);
  uint8_t is_head = responders.head->value == r;

  if (is_head) {
    uint32_t eflags = interrupt_save_disable();
    struct point bg_dst = window_chrome_pos(r->window_pos);
    struct point bg_src = { 0, 0 };
    struct dim bg_dim = window_chrome_dim(r->window_dim);
    if (clip_rect_on_screen(&bg_dst, &bg_src, &bg_dim)) {
      bool redraw_cursor =
        rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, bg_dst, bg_dim);
      if (redraw_cursor)
        clear_cursor();
      copy_rect(back_buffer, bg_dst, r->bg_buf, bg_src, bg_dim);
      if (redraw_cursor) {
        cursor_pixels = CURSOR_DEFAULT_PIXELS;
        blit_cursor();
      }
      copy_rect(frame_buffer, bg_dst, back_buffer, bg_dst, bg_dim);
    }
    interrupt_restore(eflags);
  }

  list_remove(&responders, r->list_node, 1);
  if (is_head && responders.size) {
    uint32_t err = dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
    CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  }

  kunlock(&responders_lock);

  if (!is_head)
    redraw_all();
  return 0;
}

uint32_t ui_redraw_rect(process_t *p, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
  uint32_t eflags = interrupt_save_disable();
  struct responder *r = responders_by_gid[p->gid];
  CHECK_RESTORE_EFLAGS(r == NULL, "No responders available", 1);

  if (r == responders.head->value) {
    if (r->window_drawn) {
      const struct point origin = { (int32_t)x, (int32_t)y };
      const struct dim dim = { w, h };
      redraw_key_responder(origin, dim);
    } else {
      r->window_drawn = true;
      struct point dst = window_chrome_pos(r->window_pos);
      struct point src = { 0, 0 };
      struct dim dim = window_chrome_dim(r->window_dim);
      if (clip_rect_on_screen(&dst, &src, &dim)) {
        bool redraw_mouse =
          rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, dst, dim);
        if (redraw_mouse)
          clear_cursor();
        blit_window(r);
        if (redraw_mouse)
          blit_cursor();
        copy_rect(frame_buffer, dst, back_buffer, dst, dim);
      }
    }
  } else
    redraw_all();

  interrupt_restore(eflags);
  return 0;
}

uint32_t ui_yield(process_t *p)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  if (r == NULL) {
    kunlock(&responders_lock);
    return 1;
  }

  if (responders.head->value != r || responders.size <= 1) {
    kunlock(&responders_lock);
    return 0;
  }

  // Rotate responders list
  list_remove(&responders, responders.head, 0);
  r->list_node->prev = responders.tail;
  r->list_node->next = NULL;
  responders.tail->next = r->list_node;
  responders.tail = r->list_node;
  responders.size++;

  uint32_t err = dispatch_window_event(r, UI_EVENT_SLEEP);
  CHECK_UNLOCK_R(err, "Failed to dispatch sleep event.", err);
  err = dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
  CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);

  kunlock(&responders_lock);

  redraw_all();
  return 0;
}

uint32_t ui_next_event(process_t *p, uint32_t buf)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  if (r == NULL) {
    kunlock(&responders_lock);
    return 1;
  }
  // Do not hold responders lock while blocking on event_pipe_read
  kunlock(&responders_lock);

  uint8_t ev_buf[sizeof(ui_event_t)];
  uint32_t read_size = fs_read(&r->event_pipe_read, 0, sizeof(ui_event_t), ev_buf);
  if (read_size < sizeof(ui_event_t))
    return 1;

  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);
  u_memcpy((uint8_t *)buf, ev_buf, sizeof(ui_event_t));
  paging_set_cr3(cr3);
  interrupt_restore(eflags);

  return 0;
}

uint32_t ui_poll_events(process_t *p)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  uint32_t count = 0;
  if (r != NULL)
    count = r->event_pipe_read.length / sizeof(ui_event_t);
  kunlock(&responders_lock);
  return count;
}

uint32_t ui_set_wallpaper(const char *path)
{
  uint32_t eflags = interrupt_save_disable();
  fs_node_t wallpaper_node;
  uint32_t err = fs_open_node(&wallpaper_node, path, 0);
  CHECK_RESTORE_EFLAGS(err, "Failed to open wallpaper file", err);

  uint32_t n = fs_read(&wallpaper_node, 0, wallpaper_node.length, (uint8_t *)wallpaper);
  CHECK_RESTORE_EFLAGS(n != (frame_size * sizeof(uint32_t)), "Failed to read wallpaper file", n);

  redraw_all();
  interrupt_restore(eflags);
  return 0;
}

uint32_t ui_resize_window(process_t *p, uint32_t buf, uint32_t w, uint32_t h)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  CHECK_UNLOCK_R(r == NULL, "Process does not have window.", 1);

  uint32_t eflags = interrupt_save_disable();
  kunlock(&responders_lock);

  struct dim old_dim = window_chrome_dim(r->window_dim);
  if (r == responders.head->value) {
    struct point dst = window_chrome_pos(r->window_pos);
    struct point src = { 0, 0 };
    if (clip_rect_on_screen(&dst, &src, &old_dim)) {
      bool redraw_cursor =
        rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, dst, old_dim);
      if (redraw_cursor)
        clear_cursor();
      copy_rect(back_buffer, dst, r->bg_buf, src, old_dim);
      if (redraw_cursor)
        blit_cursor();
    }
  }

  const struct dim window_chrome_dims = window_chrome_dim((struct dim){ 0, 0 });
  r->window_dim.w = min(max(w, TITLE_BAR_WIDTH), SCREENWIDTH - window_chrome_dims.w);
  r->window_dim.h = min(max(h, 50), SCREENHEIGHT - window_chrome_dims.h);
  r->resize_dim = r->window_dim;
  r->buf = (struct pixel_buffer){ (uint32_t *)buf, r->window_dim.w };
  kfree(r->bg_buf.buf);
  const struct dim bg_buf_dim = window_chrome_dim(r->window_dim);
  const uint32_t bg_buf_w = bg_buf_dim.w;
  const uint32_t bg_buf_h = bg_buf_dim.h;
  r->bg_buf.buf = kmalloc(bg_buf_w * bg_buf_h * sizeof(uint32_t));
  CHECK_RESTORE_EFLAGS(r->bg_buf.buf == NULL, "No memory.", ENOMEM);
  r->bg_buf.stride = bg_buf_dim.w;

  if (r == responders.head->value) {
    struct point dst = window_chrome_pos(r->window_pos);
    struct point src = { 0, 0 };
    struct dim dim = window_chrome_dim(r->window_dim);
    if (clip_rect_on_screen(&dst, &src, &dim)) {
      bool redraw_cursor =
        rect_intersect(mouse_pos, (struct dim){ CURSOR_WIDTH, CURSOR_HEIGHT }, dst, dim);
      if (redraw_cursor)
        clear_cursor();
      blit_window(r);
      struct dim redraw_dim = { max(old_dim.w, dim.w), max(old_dim.h, dim.h) };
      if (redraw_cursor) {
        blit_cursor();
        const uint32_t cursor_rect_w = mouse_pos.x + CURSOR_WIDTH - dst.x;
        redraw_dim.w = max(redraw_dim.w, cursor_rect_w);
        const uint32_t cursor_rect_h = mouse_pos.y + CURSOR_HEIGHT - dst.y;
        redraw_dim.h = max(redraw_dim.h, cursor_rect_h);
      }
      copy_rect(frame_buffer, dst, back_buffer, dst, redraw_dim);
    }
  } else
    redraw_all();

  interrupt_restore(eflags);
  return 0;
}

uint32_t ui_enable_mouse_move_events(process_t *p)
{
  klock(&responders_lock);
  struct responder *r = responders_by_gid[p->gid];
  CHECK_UNLOCK_R(r == NULL, "Process does not have window.", 1);
  r->mouse_move_events_enabled = true;
  kunlock(&responders_lock);
  return 0;
}
