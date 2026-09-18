#ifndef FB_H
#define FB_H

#include <stdint.h>
#include "multiboot.h"

#define FB_DEFAULT_WIDTH  1024
#define FB_DEFAULT_HEIGHT 768
#define FB_DEFAULT_BPP    32

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

void fb_init(struct mb_info *info);
int fb_is_active(void);
uint32_t fb_get_addr(void);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_pitch(void);

int fb_ioctl(uint32_t request, uint32_t arg);

void fb_clear(uint32_t color);
void fb_draw_char(unsigned char c, int col, int row, uint8_t color_attr);
void fb_scroll_up(int char_rows);
void fb_fill_cells(int col0, int row0, int col1, int row1, uint8_t color_attr);

#endif /* FB_H */

