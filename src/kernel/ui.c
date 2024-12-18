
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ui.h"
#include "../common/scancode.h"
#include "constants.h"
#include "ds.h"
#include "fs.h"
#include "interrupt.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "paging.h"
#include "pipe.h"
#include "pmm.h"
#include "process.h"
#include "ui_cursor.h"
#include "ui_font_data.h"
#include "ui_title_bar.h"
#include "util.h"
#include <stdbool.h>

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
  char window_title[20];
  struct point window_pos;
  struct dim window_dim;
  struct dim resize_dim;
  uint8_t window_opacity;
  bool window_drawn;
  bool window_moving;
  bool window_resizing_w;
  bool window_resizing_h;
  struct pixel_buffer buf;
  struct pixel_buffer bg_buf;
  fs_node_t event_pipe_read;
  fs_node_t event_pipe_write;
  list_node_t *list_node;
};

static const unsigned window_shadow_size = 5;
static const uint32_t window_shadow_color = 0x555555;

static const size_t frame_size = SCREENWIDTH * SCREENHEIGHT;
static uint32_t *wallpaper = NULL;
static struct pixel_buffer cursor_bg_buffer = { NULL, 0 };
static struct pixel_buffer back_buffer = { NULL, 0 };
static struct pixel_buffer frame_buffer = { NULL, 0 };

struct point mouse_pos = { 100, 100 };
static uint8_t mouse_left_clicked = 0;
static const uint8_t *cursor_pixels = CURSOR_DEFAULT_PIXELS;
static bool cursor_flipped = false;
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

static void fill_rect(struct pixel_buffer buf,
                      struct point pos,
                      const struct pixel_buffer alpha_bg,
                      struct point alpha_bg_point,
                      struct dim dim,
                      uint32_t color,
                      uint8_t opacity)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    const uint32_t buf_offset = (pos.y + y) * buf.stride + pos.x;
    const uint32_t alpha_bg_offset = (alpha_bg_point.y + y) * alpha_bg.stride + alpha_bg_point.x;
    if (opacity == 0xff)
      u_memset32(buf.buf + buf_offset, color, dim.w);
    else
      for (uint32_t x = 0; x < dim.w; ++x)
        buf.buf[buf_offset + x] = blend_alpha(alpha_bg.buf[alpha_bg_offset + x], color, opacity);
  }
}

static void invert_rect(struct pixel_buffer buf, struct point pos, struct dim dim)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    uint32_t offset = (pos.y + y) * buf.stride + pos.x;
    for (uint32_t x = 0; x < dim.w; ++x)
      buf.buf[offset + x] = 0xffffff - buf.buf[offset + x];
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

static void blit_window(struct responder *r)
{
  const process_t *p = r->process;
  const uint32_t eflags = interrupt_save_disable();
  const uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  struct point bg_buf_src = { r->window_pos.x, r->window_pos.y - TITLE_BAR_HEIGHT };
  struct point bg_buf_dst = { 0, 0 };
  struct dim bg_buf_dim = { r->window_dim.w + window_shadow_size,
                            r->window_dim.h + TITLE_BAR_HEIGHT + window_shadow_size };
  if (clip_rect_on_screen(&bg_buf_src, &bg_buf_dst, &bg_buf_dim))
    copy_rect(r->bg_buf, bg_buf_dst, back_buffer, bg_buf_src, bg_buf_dim);

  struct point title_bar_dst = { r->window_pos.x, r->window_pos.y - TITLE_BAR_HEIGHT };
  struct point title_bar_src = { 0, 0 };
  struct dim title_bar_dim = { TITLE_BAR_WIDTH, TITLE_BAR_HEIGHT };
  if (clip_rect_on_screen(&title_bar_dst, &title_bar_src, &title_bar_dim)) {
    static uint32_t title_bar_tmp_buf[sizeof(TITLE_BAR_PIXELS)];
    u_memcpy32(title_bar_tmp_buf, TITLE_BAR_PIXELS, TITLE_BAR_HEIGHT * TITLE_BAR_WIDTH);
    struct point window_title_dst = { TITLE_BAR_BUTTON_WIDTH + 4, 2 };
    const struct pixel_buffer title_bar_buf = { title_bar_tmp_buf, TITLE_BAR_WIDTH };
    render_text(title_bar_buf, window_title_dst, r->window_title);

    copy_rect_alpha(back_buffer,
                    title_bar_dst,
                    title_bar_buf,
                    title_bar_src,
                    r->bg_buf,
                    title_bar_src,
                    title_bar_dim,
                    r->window_opacity);
  }

  struct point tb_shadow_dst = { .x = r->window_pos.x + TITLE_BAR_WIDTH,
                                 .y = r->window_pos.y - TITLE_BAR_HEIGHT + window_shadow_size };
  struct point tb_shadow_src = { 0, 0 };
  struct dim tb_shadow_dim = { window_shadow_size, TITLE_BAR_HEIGHT - window_shadow_size };
  if (clip_rect_on_screen(&tb_shadow_dst, &tb_shadow_src, &tb_shadow_dim))
    fill_rect(back_buffer,
              tb_shadow_dst,
              r->bg_buf,
              (struct point){ TITLE_BAR_WIDTH, window_shadow_size },
              tb_shadow_dim,
              window_shadow_color,
              r->window_opacity);

  struct point window_dst = r->window_pos;
  struct point window_src = { 0, 0 };
  struct dim window_dim = r->window_dim;
  if (clip_rect_on_screen(&window_dst, &window_src, &window_dim))
    copy_rect_alpha(back_buffer,
                    window_dst,
                    r->buf,
                    window_src,
                    r->bg_buf,
                    (struct point){ window_src.x, window_src.y + TITLE_BAR_HEIGHT },
                    window_dim,
                    r->window_opacity);

  struct point rshadow_dst = { .x = r->window_pos.x + r->window_dim.w,
                               .y = r->window_pos.y + window_shadow_size };
  struct point rshadow_src = { 0, 0 };
  struct dim rshadow_dim = { window_shadow_size, r->window_dim.h - window_shadow_size };
  if (clip_rect_on_screen(&rshadow_dst, &rshadow_src, &rshadow_dim))
    fill_rect(back_buffer,
              rshadow_dst,
              r->bg_buf,
              (struct point){ r->window_dim.w, TITLE_BAR_HEIGHT + window_shadow_size },
              rshadow_dim,
              window_shadow_color,
              r->window_opacity);

  struct point bshadow_dst = { .x = r->window_pos.x + window_shadow_size,
                               .y = r->window_pos.y + r->window_dim.h };
  struct point bshadow_src = { 0, 0 };
  struct dim bshadow_dim = { r->window_dim.w, window_shadow_size };
  if (clip_rect_on_screen(&bshadow_dst, &bshadow_src, &bshadow_dim))
    fill_rect(back_buffer,
              bshadow_dst,
              r->bg_buf,
              (struct point){ window_shadow_size, r->window_dim.h + TITLE_BAR_HEIGHT },
              bshadow_dim,
              window_shadow_color,
              r->window_opacity);

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
}

static void blit_cursor()
{
  uint32_t cursor_w = min(mouse_pos.x + CURSOR_WIDTH, SCREENWIDTH) - mouse_pos.x;
  uint32_t cursor_h = min(mouse_pos.y + CURSOR_HEIGHT, SCREENHEIGHT) - mouse_pos.y;
  const uint32_t cursor_colors[] = { 0, 0xffffff };

  if (cursor_flipped) {
    for (uint32_t y = 0; y < cursor_h; ++y) {
      const uint32_t row_offset = (mouse_pos.y + y) * SCREENWIDTH;
      for (uint32_t x = 0; x < cursor_w; ++x) {
        const uint32_t pixel_offset = row_offset + mouse_pos.x + x;
        cursor_bg_buffer.buf[y * CURSOR_WIDTH + x] = back_buffer.buf[pixel_offset];
        uint8_t cursor_pixel = cursor_pixels[x * CURSOR_WIDTH + y];
        if (cursor_pixel == CURSOR_NONE)
          continue;
        back_buffer.buf[pixel_offset] = cursor_colors[cursor_pixel];
      }
    }
    return;
  }

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

  const uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(r->process->cr3);

  copy_rect_alpha(back_buffer,
                  dst,
                  r->buf,
                  src,
                  r->bg_buf,
                  (struct point){ src.x, src.y + TITLE_BAR_HEIGHT },
                  clipped_dim,
                  r->window_opacity);
  paging_set_cr3(cr3);

  if (mouse_in_rect(dst.x, dst.y, clipped_dim.w - CURSOR_WIDTH, clipped_dim.h - CURSOR_HEIGHT))
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
      moved_object_w = key_responder->window_dim.w + window_shadow_size;
      moved_object_h = key_responder->window_dim.h + TITLE_BAR_HEIGHT + window_shadow_size;
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
                      window_shadow_size,
                      window_shadow_size)) {
      new_key_responder = r;
      r->window_resizing_w = true;
      r->window_resizing_h = true;
      break;
    }
    if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                      r->window_pos.y + window_shadow_size,
                      window_shadow_size,
                      r->window_dim.h - window_shadow_size)) {
      new_key_responder = r;
      r->window_resizing_w = true;
      break;
    }
    if (mouse_in_rect(r->window_pos.x + window_shadow_size,
                      r->window_pos.y + r->window_dim.h,
                      r->window_dim.w - window_shadow_size,
                      window_shadow_size)) {
      new_key_responder = r;
      r->window_resizing_h = true;
      break;
    }
  }

  if (new_key_responder == NULL)
    return;

  if (new_key_responder != responders.head->value) {
    list_remove(&responders, new_key_responder->list_node, 0);
    new_key_responder->list_node->next = responders.head;
    new_key_responder->list_node->prev = NULL;
    responders.head->prev = new_key_responder->list_node;
    responders.head = new_key_responder->list_node;
    responders.size++;
  }

  redraw_all();

  if (new_key_responder->window_resizing_w || new_key_responder->window_resizing_h) {
    struct point invert_dst = new_key_responder->window_pos;
    struct point invert_src = { 0, 0 };
    struct dim invert_dim = new_key_responder->window_dim;
    if (clip_rect_on_screen(&invert_dst, &invert_src, &invert_dim)) {
      invert_rect(back_buffer, invert_dst, invert_dim);
      copy_rect(frame_buffer, invert_dst, back_buffer, invert_dst, invert_dim);
    }
  }
}

static void handle_mouse_scroll(int8_t vscroll, int8_t hscroll)
{
  list_foreach(node, &responders)
  {
    struct responder *r = node->value;
    if (mouse_in_rect(r->window_pos.x, r->window_pos.y, r->window_dim.w, r->window_dim.h)) {
      ui_event_t ev;
      ev.type = UI_EVENT_SCROLL;
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
  cursor_flipped = false;

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
                    window_shadow_size,
                    window_shadow_size)) {
    cursor_pixels = CURSOR_RESIZE_DIAG_PIXELS;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + r->window_dim.w,
                    r->window_pos.y + window_shadow_size,
                    window_shadow_size,
                    r->window_dim.h - window_shadow_size)) {
    cursor_pixels = CURSOR_RESIZE_PIXELS;
    cursor_flipped = true;
    return;
  }
  if (mouse_in_rect(r->window_pos.x + window_shadow_size,
                    r->window_pos.y + r->window_dim.h,
                    r->window_dim.w - window_shadow_size,
                    window_shadow_size)) {
    cursor_pixels = CURSOR_RESIZE_PIXELS;
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
      key_responder->window_moving = false;
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
      }
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
      const uint32_t old_window_x = key_responder->window_pos.x;
      const uint32_t old_window_y = key_responder->window_pos.y;
      key_responder->window_pos.x += mouse_pos.x - old_mouse_pos.x;
      key_responder->window_pos.y += mouse_pos.y - old_mouse_pos.y;
      struct point old = { .x = old_window_x, .y = old_window_y - TITLE_BAR_HEIGHT };
      struct point new = { .x = key_responder->window_pos.x,
                           .y = key_responder->window_pos.y - TITLE_BAR_HEIGHT };
      redraw_moving_objects(old, new);
      return 0;
    } else if (key_responder->window_resizing_w || key_responder->window_resizing_h) {
      const struct dim old_resize_dim = key_responder->resize_dim;
      if (key_responder->window_resizing_w) {
        key_responder->resize_dim.w += mouse_pos.x - old_mouse_pos.x;
        key_responder->resize_dim.w =
          min(max(key_responder->resize_dim.w, TITLE_BAR_WIDTH), SCREENWIDTH);
      }
      if (key_responder->window_resizing_h) {
        key_responder->resize_dim.h += mouse_pos.y - old_mouse_pos.y;
        key_responder->resize_dim.h =
          min(max(key_responder->resize_dim.h, 50), SCREENHEIGHT - TITLE_BAR_HEIGHT);
      }
      redraw_resizing_window(
        old_mouse_pos, key_responder->window_pos, old_resize_dim, key_responder->resize_dim);
      return 0;
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
  r->process = p;
  r->window_dim.w = min(max(w, TITLE_BAR_WIDTH), SCREENWIDTH);
  r->window_dim.h = min(max(h, 50), SCREENHEIGHT - TITLE_BAR_HEIGHT);
  r->resize_dim = r->window_dim;
  r->buf = (struct pixel_buffer){ (uint32_t *)buf, r->window_dim.w };
  r->bg_buf.buf =
    kmalloc((r->window_dim.w + window_shadow_size) *
            (r->window_dim.h + TITLE_BAR_HEIGHT + window_shadow_size) * sizeof(uint32_t));
  CHECK_UNLOCK_R(r->bg_buf.buf == NULL, "No memory.", ENOMEM);
  r->bg_buf.stride = r->window_dim.w + window_shadow_size;
  r->window_opacity = 0xff;
  r->window_drawn = false;
  size_t title_len = u_strlen(title);
  u_memcpy(r->window_title, title, min(title_len, sizeof(r->window_title)));
  r->window_pos.x = SCREENWIDTH >> 2;
  r->window_pos.y = SCREENHEIGHT >> 2;

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
  list_remove(&responders, r->list_node, 1);

  if (is_head && responders.size) {
    uint32_t err = dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
    CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  }

  kunlock(&responders_lock);
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
      blit_window(r);
      struct point dst = { r->window_pos.x, r->window_pos.y - TITLE_BAR_HEIGHT };
      struct point src = { 0, 0 };
      struct dim dim = { r->window_dim.w + window_shadow_size,
                         r->window_dim.h + TITLE_BAR_HEIGHT + window_shadow_size };
      if (clip_rect_on_screen(&dst, &src, &dim))
        copy_rect(frame_buffer, dst, back_buffer, dst, dim);
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

  r->window_dim.w = min(max(w, TITLE_BAR_WIDTH), SCREENWIDTH);
  r->window_dim.h = min(max(h, 50), SCREENHEIGHT - TITLE_BAR_HEIGHT);
  r->resize_dim = r->window_dim;
  r->buf = (struct pixel_buffer){ (uint32_t *)buf, r->window_dim.w };
  kfree(r->bg_buf.buf);
  r->bg_buf.buf =
    kmalloc((r->window_dim.w + window_shadow_size) *
            (r->window_dim.h + TITLE_BAR_HEIGHT + window_shadow_size) * sizeof(uint32_t));
  CHECK_RESTORE_EFLAGS(r->bg_buf.buf == NULL, "No memory.", ENOMEM);
  r->bg_buf.stride = r->window_dim.w + window_shadow_size;

  redraw_all();
  interrupt_restore(eflags);
  return 0;
}
