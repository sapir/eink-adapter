#ifndef __EINK_H__
#define __EINK_H__


#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>
#include <stdint.h>


#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH  800


bool eink_setup(void);
void eink_power_on(void);
void eink_power_off(void);

typedef int pixel_t;
#define PIXEL_BIT_SIZE 4
#define WHITE 0
#define BLACK 15


#define PIXEL_BITMASK ((1<<PIXEL_BIT_SIZE) - 1)
#define PIXELS_PER_BYTE (8 / PIXEL_BIT_SIZE)
#define MAX_BITMAP_ROW_SIZE (SCREEN_WIDTH / PIXELS_PER_BYTE)

static inline pixel_t get_row_pixel(const uint8_t *row, int x) {
    const int byte_index = x / PIXELS_PER_BYTE;
    const int bit_index = 8 - ((x % PIXELS_PER_BYTE) + 1) * PIXEL_BIT_SIZE;
    return (row[byte_index] >> bit_index) & PIXEL_BITMASK;
}

static inline void set_row_pixel(uint8_t *row, int x, pixel_t p) {
    const int byte_index = x / PIXELS_PER_BYTE;
    const int bit_index = 8 - ((x % PIXELS_PER_BYTE) + 1) * PIXEL_BIT_SIZE;
    const uint8_t shifted_bitmask = PIXEL_BITMASK << bit_index;
    const uint8_t shifted_pixel = (p & PIXEL_BITMASK) << bit_index;
    row[byte_index] = (row[byte_index] & ~shifted_bitmask) | shifted_pixel;
}


// callback that generates both the row to be replaced and the new row to be
// drawn.
// output is in bitmap format, 1 bit per pixel, leftmost pixel in MSB.
// callback can return false to say drawing should stop.
// callback is guaranteed to be called in increasing y order.
typedef bool (*get_rows_cb_t)(void *arg, int y,
    int x0, int x1, uint8_t *old_row_bitmap, uint8_t *new_row_bitmap);

// draw from (x0, y0)-(x1, y1)
// returns true if drawing was completed
bool eink_update(get_rows_cb_t get_rows_cb, void *cb_arg,
    int x0, int y0, int x1, int y1);

// returns true if drawing was completed
bool eink_full_update(get_rows_cb_t get_rows_cb, void *cb_arg);

// note this only works for full white/black
void eink_refresh(pixel_t pixel);


#ifdef __cplusplus
} // extern "C"
#endif


#endif
