#include "espressif/esp_common.h"
#include "espressif/esp_misc.h"
#include <string.h>
#include "esp/gpio.h"
#include "esp/spi.h"
#include "esp/interrupts.h"
#include "wemos_d1_mini.h"
#include "eink.h"
#include "waveform.h"


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


// pixel values in a byte
#define PVS_PER_IO_BYTE 4

#define QUAD_PIXEL_VALUE(val) \
    ((val) | ((val)<<2) | ((val)<<4) | ((val)<<6))


// delay used for vscan_write when writing clear rows
#define CLEAR_WRITE_TIME_NS     5000


static void delay_ms(uint32_t ms)
{
    for (int i = 0; i < ms; ++i) {
        sdk_os_delay_us(1000);
    }
}

static void delay_us(uint32_t us)
{
    delay_ms(us / 1000);
    sdk_os_delay_us(us % 1000);
}

// TODO: only good for 50ns+, and ignores function call time
static inline void delay_25ns_steps(int steps)
{
    // at 80MHz, each cycle is 12.5ns, so 2 cycles are 25ns (1 step).
    // TODO: this naively assumes that each instruction is 1 cycle.
    register int i;
    __asm__ __volatile__ (
            "nop\n"
            "addi  %0, %1, -1\n"
        "0:\n"
            "addi  %0, %0, -1\n"
            "bgez  %0, 0b\n"
        : "=r"(i) : "r"(steps));
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

static inline void high(uint16_t pins)
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

    delay_us(100);
    high(BIT_VNEG);

    delay_us(1000);
    high(BIT_VPOS);

    delay_us(10);
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
    high(BIT_OE);
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
    hclk(2);

    high(BIT_LE);
    hclk(2);
    low(BIT_LE);
    hclk(2);
}

static void hscan_solid_row(int pixel_val)
{
    data_write(QUAD_PIXEL_VALUE(pixel_val));
    hscan_start();
    hclk(SCREEN_WIDTH/PVS_PER_IO_BYTE);
    hscan_stop();
}


static void vclk(int n)
{
    for (int i = 0; i < n; ++i) {
        low(BIT_CKV);
        delay_25ns_steps(60*20);
        high(BIT_CKV);
        delay_25ns_steps(60*20);
    }
}

// delays in nanoseconds
static void vscan_write(uint32_t ckv_high_delay, uint32_t ckv_low_delay)
{
    uint32_t high_steps = (ckv_high_delay + 24) / 25;
    uint32_t low_steps = (ckv_low_delay + 24) / 25;

    // don't let interrupts affect timing
    uint32_t old_interrupts = _xt_disable_interrupts();

    high(BIT_OE|BIT_CKV);
    delay_25ns_steps(high_steps);
    low(BIT_CKV);
    delay_25ns_steps(low_steps);
    low(BIT_OE);

    _xt_restore_interrupts(old_interrupts);

    hclk(2);
}


static void vscan_start(void)
{
    high(BIT_GMODE);
    delay_us(1000);

    high(BIT_SPV);
    vclk(2);
    low(BIT_SPV);
    vclk(2);
    high(BIT_SPV);
    vclk(2);
}

static void vscan_stop(void)
{
    hscan_solid_row(PV_NEUTRAL);
    vscan_write(CLEAR_WRITE_TIME_NS, CLEAR_WRITE_TIME_NS);

    delay_us(1);
    low(BIT_CKV|BIT_OE);
    delay_us(3000);
    high(BIT_CKV);
    delay_us(430);
    low(BIT_CKV);
    delay_us(1);
    low(BIT_GMODE);
    delay_us(1);
}


// do one stage of updating old row -> new_row
static void do_row_update_stage(int x0, int x1,
    uint8_t *old_row, uint8_t *new_row, int wf_stage)
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
                pixel_val = get_update_waveform_value(wf_stage,
                    old_pixel, new_pixel);
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

    int y;
    uint8_t old_row[MAX_BITMAP_ROW_SIZE] = {};
    uint8_t new_row[MAX_BITMAP_ROW_SIZE] = {};

    uint32_t ckv_high_delay_ns;
    uint32_t ckv_low_delay_ns;
    // real stop condition is after get_update_waveform_timings
    for (int wf_stage = 0; !stopped; ++wf_stage) {
        get_update_waveform_timings(wf_stage,
            &ckv_high_delay_ns, &ckv_low_delay_ns);
        if (0 == ckv_high_delay_ns) {
            break;
        }

        vscan_start();

        y = 0;

        if (y < y0) {
            hscan_solid_row(PV_NEUTRAL);
            for (; y < y0; ++y) {
                vscan_write(CLEAR_WRITE_TIME_NS, CLEAR_WRITE_TIME_NS);
            }
        }


        for (; y < y1; ++y) {
            if (!get_rows_cb(cb_arg, y, x0, x1, old_row, new_row)) {
                stopped = true;
                break;
            }

            do_row_update_stage(x0, x1, old_row, new_row, wf_stage);

            vscan_write(ckv_high_delay_ns, ckv_low_delay_ns);
        }

        if (y < SCREEN_HEIGHT) {
            hscan_solid_row(PV_NEUTRAL);
            for (; y < SCREEN_HEIGHT; ++y) {
                vscan_write(CLEAR_WRITE_TIME_NS, CLEAR_WRITE_TIME_NS);
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
    uint32_t ckv_high_delay_ns;
    uint32_t ckv_low_delay_ns;
    // stop condition is after get_update_waveform_timings
    for (int wf_stage = 0; ; ++wf_stage) {
        get_refresh_waveform_timings(wf_stage,
            &ckv_high_delay_ns, &ckv_low_delay_ns);
        if (0 == ckv_high_delay_ns) {
            break;
        }

        vscan_start();

        hscan_solid_row(get_refresh_waveform_value(wf_stage, pixel));
        // there seem to be extra rows visible, so +10
        for (int y = 0; y < SCREEN_HEIGHT + 10; ++y) {
            vscan_write(ckv_high_delay_ns, ckv_low_delay_ns);
        }

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
