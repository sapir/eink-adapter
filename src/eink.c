#include "espressif/esp_common.h"
#include "espressif/esp_misc.h"
#include <string.h>
#include "esp/gpio.h"
#include "esp/spi.h"
#include "wemos_d1_mini.h"
#include "eink.h"


// SPI used for shift register
#define SR_SPI 1


// pins not included in shift registers:
#define PIN_SR_N_OE     PIN_D4      // shift register not-output-enable
#define PIN_CL          PIN_D2      // horizontal clock
// PIN_D1 conflicts with SPI-?
#define PIN_OE          PIN_D3      // display output enable
// SPI pins:
//      SRCLK           PIN_D5      // GPIO14
//      DATA            PIN_D7      // GPIO13
//      LATCH           PIN_D8      // GPIO15


// control bits: (low bits belong to shift register, top bits use extra pins)
#define BIT_SMPS        (1<<0)      // SMPS enable, active low
#define BIT_VNEG        (1<<1)      // -20V/-15V enable, active high
#define BIT_VPOS        (1<<2)      // +22V/+15V enable, active high
#define BIT_GMODE       (1<<3)      // GMODE, actually a 2-bit value but we
                                        // tied the 2 bits. 00 is off, 11 is on
#define BIT_CKV         (1<<4)      // vertical clock
#define BIT_SPV         (1<<5)      // start pulse vertical, active low
#define BIT_SPH         (1<<6)      // start pulse horizontal, active low
#define BIT_LE          (1<<7)      // source (horiz.) driver latch enable
#define BIT_CL          (1<<8)      // horizontal clock
#define BIT_OE          (1<<9)      // source (horiz.) driver output enable
                                        // (active high + CKV must be high?)
#define SR_BITS_MASK    0x00ff
#define EXTRA_BITS_MASK 0xff00

static uint16_t eink_ctl;
static uint8_t eink_data_byte;


enum WAVEFORM {
    WF_W2W,
    WF_W2B,
    WF_B2W,
    WF_B2B,

    NUM_WAVEFORMS,
};

enum PIXEL_VALUE {
    PV_NEUTRAL = 0,
    PV_BLACK, // +15V
    PV_WHITE, // -15V
    PV_HIZ,
};

// pixel values in a byte
#define PVS_PER_IO_BYTE 4

#define QUAD_PIXEL_VALUE(val) \
    ((val) | ((val)<<2) | ((val)<<4) | ((val)<<6))


// TODO: unknown->white, unknown->black?
// TODO: does the old pixel value really matter?

struct waveform_stage {
    unsigned int length_us;
    enum PIXEL_VALUE values[NUM_WAVEFORMS];
};

static const struct waveform_stage refresh_waveforms[] = {
//    (us)     W->W        W->B        B->W        B->B
    {   20, { PV_NEUTRAL, PV_BLACK,  PV_WHITE,   PV_NEUTRAL } },
    {  120, { PV_NEUTRAL, PV_WHITE,  PV_BLACK,   PV_NEUTRAL } },
    {  100, { PV_NEUTRAL, PV_BLACK,  PV_WHITE,   PV_NEUTRAL } },

    // null stage to signify end of waveform
    {  0, {} },
};

static const struct waveform_stage normal_waveforms[] = {
//    (us)     W->W        W->B        B->W        B->B
    {    8, { PV_BLACK,   PV_NEUTRAL, PV_NEUTRAL, PV_WHITE,   } },
    {   12, { PV_BLACK,   PV_BLACK,   PV_WHITE,   PV_WHITE,   } },
    {   20, { PV_WHITE,   PV_WHITE,   PV_BLACK,   PV_BLACK,   } },
    {    8, { PV_NEUTRAL, PV_BLACK,   PV_WHITE,   PV_NEUTRAL, } },

    // null stage to signify end of waveform
    {  0, {} },
};


static void delay_ms(int ms)
{
    for (int i = 0; i < ms; ++i) {
        sdk_os_delay_us(1000);
    }
}


static inline uint16_t make_sr_val(uint16_t ctl, uint8_t data_byte)
{
    uint16_t sr_ctl = (ctl & SR_BITS_MASK);
    return ((uint16_t)sr_ctl << 8) | (uint8_t)data_byte;
}

// updates shift register only, not extra control pins
static void update_sr(void)
{
    spi_transfer_16(SR_SPI, make_sr_val(eink_ctl, eink_data_byte));
}

// updates extra control pins
static void update_extra(void)
{
    gpio_write(PIN_CL, (eink_ctl & BIT_CL) ? 1 : 0);
    gpio_write(PIN_OE, (eink_ctl & BIT_OE) ? 1 : 0);
}

static void update_ctl(void)
{
    update_sr();
    update_extra();
}

static inline void low(uint16_t pins)
{
    eink_ctl &= ~pins;
    if (pins & SR_BITS_MASK)
        update_sr();
    if (pins & EXTRA_BITS_MASK)
        update_extra();
}

static void high(uint16_t pins)
{
    eink_ctl |= pins;
    if (pins & SR_BITS_MASK)
        update_sr();
    if (pins & EXTRA_BITS_MASK)
        update_extra();
}


void eink_power_on(void)
{
    // clear everything (turning SMPS on), clear VNEG & VPOS
    eink_ctl = 0;
    update_ctl();

    sdk_os_delay_us(100);
    high(BIT_VNEG);

    sdk_os_delay_us(1000);
    high(BIT_VPOS);

    sdk_os_delay_us(10);
    high(BIT_SPV|BIT_SPH);
}

void eink_power_off(void)
{
    // turn everything off except voltage stuff
    eink_ctl &= (BIT_SMPS|BIT_VPOS|BIT_VNEG);

    delay_ms(10);

    low(BIT_VPOS);
    delay_ms(10);
    low(BIT_VNEG);
    delay_ms(100);
    // turn SMPS off by setting this bit
    high(BIT_SMPS);
}


static void hclk(int n)
{
    for (int i = 0; i < n; ++i) {
        high(BIT_CL);
        low(BIT_CL);
    }
}


static void hscan_start(void)
{
    hclk(2);
    low(BIT_SPH);
}

static void data_write(uint8_t b)
{
    // we often have runs of the same data byte
    if (b != eink_data_byte) {
        eink_data_byte = b;
        update_sr();
    }
    hclk(1);
}

static void hscan_stop(void)
{
    high(BIT_SPH);

    hclk(1);

    high(BIT_LE);
    sdk_os_delay_us(1);
    low(BIT_LE);
    sdk_os_delay_us(1);
}

static void hscan_solid_row(int p)
{
    data_write(QUAD_PIXEL_VALUE(p));
    hscan_start();
    hclk(SCREEN_WIDTH/PVS_PER_IO_BYTE);
    hscan_stop();
}


static void vclk(void)
{
    high(BIT_CKV);
    sdk_os_delay_us(1);
    low(BIT_CKV);
}

static void vscan_write(uint16_t delay)
{
    // TODO: combine delay with write time
    // TODO: try overlapping delays of different rows using PV_HIZ

    low(BIT_CKV);
    sdk_os_delay_us(1);
    high(BIT_OE);
    sdk_os_delay_us(delay);
    high(BIT_CKV);
    sdk_os_delay_us(1);
    low(BIT_OE);
    sdk_os_delay_us(1);

    vclk();
}

static void vscan_start(void)
{
    eink_ctl |= (BIT_GMODE|BIT_CKV);
    eink_ctl &= ~(BIT_LE);
    update_ctl();
    sdk_os_delay_us(1000);
    high(BIT_SPV);
    sdk_os_delay_us(500);
    low(BIT_SPV);
    sdk_os_delay_us(1);
    low(BIT_CKV);
    sdk_os_delay_us(25);
    high(BIT_CKV);
    sdk_os_delay_us(1);
    high(BIT_SPV);

    for (int i = 0; i < 3; ++i) {
        sdk_os_delay_us(25);
        vclk();
    }
}

static void vscan_stop(void)
{
    for (int i = 0; i < 8; ++i) {
        vclk();
    }

    low(BIT_CKV);
    sdk_os_delay_us(3000);
    high(BIT_CKV);
    sdk_os_delay_us(430);
    low(BIT_GMODE|BIT_OE);
    low(BIT_CKV);
}


// do one stage of updating old row -> new_row
static void do_row_update_stage(int x0, int x1,
    uint8_t *old_row, uint8_t *new_row, const struct waveform_stage *cur_stage)
{
    hscan_start();

    for (int x = 0; x < SCREEN_WIDTH; x += PVS_PER_IO_BYTE) {
        // build data byte from pixel values
        uint8_t val = 0;
        for (int i = 0; i < PVS_PER_IO_BYTE; ++i) {
            int xi = x + i;

            int pixel_val;

            if (x0 <= xi && xi < x1) {
                pixel_t old_pixel = get_row_pixel(old_row, xi - x0);
                pixel_t new_pixel = get_row_pixel(new_row, xi - x0);

                enum WAVEFORM wf_idx =
                    (old_pixel == WHITE && new_pixel == WHITE) ? WF_W2W
                    : (old_pixel == BLACK && new_pixel == WHITE) ? WF_B2W
                    : (old_pixel == WHITE && new_pixel == BLACK) ? WF_W2B
                    : WF_B2B;

                pixel_val = cur_stage->values[wf_idx];
            } else {
                pixel_val = PV_NEUTRAL;
            }

            val <<= 2;
            val |= pixel_val;
        }

        data_write(val);
    }

    hscan_stop();
}

bool eink_update(get_rows_cb_t get_rows_cb, void *cb_arg,
    int x0, int y0, int x1, int y1)
{
    bool stopped = false;

    const struct waveform_stage *waveforms = normal_waveforms;

    int y;
    uint8_t old_row[MAX_BITMAP_ROW_SIZE] = {};
    uint8_t new_row[MAX_BITMAP_ROW_SIZE] = {};

    for (int wf_stage = 0; waveforms[wf_stage].length_us > 0 && !stopped; ++wf_stage) {
        const struct waveform_stage *cur_stage = &waveforms[wf_stage];

        vscan_start();

        y = 0;

        if (y < y0) {
            for (; y < y0; ++y) {
                vclk();
            }
        }

        for (; y < y1; ++y) {
            if (!get_rows_cb(cb_arg, y, x0, x1, old_row, new_row)) {
                stopped = true;
                break;
            }

            do_row_update_stage(x0, x1, old_row, new_row, cur_stage);

            // TODO
            vscan_write(cur_stage->length_us);
                    // progress if next stage is last stage
                    // waveforms[wf_stage + 1].length_us == 0);
        }

        if (y < SCREEN_HEIGHT) {
            for (; y < SCREEN_HEIGHT; ++y) {
                vclk();
            }
        }

        vscan_stop();
    }

    return stopped;
}

bool eink_full_update(get_rows_cb_t get_rows_cb, void *cb_arg)
{
    return eink_update(get_rows_cb, cb_arg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void eink_refresh(pixel_t pixel)
{
    enum WAVEFORM wf_idx =
        (pixel == WHITE) ? WF_B2W : WF_W2B;

    const struct waveform_stage *waveforms = refresh_waveforms;

    for (int wf_stage = 0; waveforms[wf_stage].length_us > 0; ++wf_stage) {
        vscan_start();

        const struct waveform_stage *cur_stage = &waveforms[wf_stage];

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            hscan_solid_row(cur_stage->values[wf_idx]);
            vscan_write(cur_stage->length_us);
        }

        sdk_os_delay_us(400);

        vscan_stop();
    }
}


bool eink_setup(void)
{
    // hopefully if I understand the datasheet correctly, 10MHz is safe for my
    // SN74HC595s at 3.3V
    if (!spi_init(SR_SPI, SPI_MODE0, SPI_FREQ_DIV_10M, true /*msb*/,
            SPI_BIG_ENDIAN, false /*minimal_pins*/))
    {
        return false;
    }

    gpio_enable(PIN_SR_N_OE, GPIO_OUTPUT);
    gpio_enable(PIN_CL, GPIO_OUTPUT);
    gpio_enable(PIN_OE, GPIO_OUTPUT);

    // initialize SR value to turning screen off, then enable the SR output
    eink_ctl = BIT_SMPS;
    update_ctl();
    gpio_write(PIN_SR_N_OE, 0);

    return true;
}
