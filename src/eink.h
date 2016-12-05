#ifndef __EINK_H__
#define __EINK_H__


#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH  800


bool eink_setup(void);
void eink_power_on(void);
void eink_power_off(void);

#define WHITE false
#define BLACK true
typedef bool pixel_t;


#define PIXEL_BIT_SIZE 1
#define PIXELS_PER_BYTE (8 / PIXEL_BIT_SIZE)
#define MAX_BITMAP_ROW_SIZE (SCREEN_WIDTH / PIXELS_PER_BYTE)

static inline pixel_t get_row_pixel(const uint8_t *row, int x) {
    const int byte_index = x / PIXELS_PER_BYTE;
    const int bit_index = 8 - ((x % PIXELS_PER_BYTE) + 1) * PIXEL_BIT_SIZE;
    const uint8_t bitmask = ((1<<PIXEL_BIT_SIZE) - 1) << bit_index;
    return (row[byte_index] & bitmask) ? BLACK : WHITE;
}

static inline void set_row_pixel(uint8_t *row, int x, pixel_t p) {
    const int byte_index = x / PIXELS_PER_BYTE;
    const int bit_index = 8 - ((x % PIXELS_PER_BYTE) + 1) * PIXEL_BIT_SIZE;
    const uint8_t bitmask = ((1<<PIXEL_BIT_SIZE) - 1) << bit_index;
    row[byte_index] =
        (row[byte_index] & ~bitmask) | ((p == BLACK) ? bitmask : 0);
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

void eink_refresh(pixel_t pixel);


#endif
