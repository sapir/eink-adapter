#include <stdlib.h>
#include "waveform.h"


// update waveform looks like this:
// (R is reset time and W is write time)
//
//     R-W seconds to write color (helps reset + preserve DC balance)
//      R  seconds to base color (reset to base)
//      W  seconds to write color (write final color)
//
// R is constant, but W depends on the target color.
//
// values for W are based on timings from NekoCal project.

const unsigned char timA[16] =
{
// 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  30,20,20,15,15,15,15,15,20,20,20,20,20,40,50
};
#define TOTAL_TIMA (30+20+20+15+15+15+15+15+20+20+20+20+20+40+50)

#define timB 40

#define FACTOR 25

#define RESET_TIME_NS           24000
#define RESET_STAGE             15

void get_update_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns,
    uint32_t *stage_delay_us)
{
    if (stage < 0 || stage >= 15+1+15) {
        *ckv_high_delay_ns = 0;
        *ckv_low_delay_ns = 0;
        *stage_delay_us = 0;
        return;
    }

    *ckv_low_delay_ns = FACTOR*timB;

    if (stage < RESET_STAGE - 1) {
        *ckv_high_delay_ns = FACTOR*timA[(RESET_STAGE - 1) - stage];
    } else if (stage == RESET_STAGE - 1) {
        _Static_assert(RESET_TIME_NS >= FACTOR*TOTAL_TIMA,
            "this calculation doesn't make sense");

        *ckv_high_delay_ns = FACTOR*timA[(RESET_STAGE - 1) - stage] +
                                (RESET_TIME_NS - FACTOR*TOTAL_TIMA);
    } else if (stage == RESET_STAGE) {
        *ckv_high_delay_ns = RESET_TIME_NS;
        *ckv_low_delay_ns = 500;
    } else {
        *ckv_high_delay_ns = FACTOR*timA[stage - (RESET_STAGE + 1)];
    }

    *stage_delay_us = 0;
}

enum PIXEL_VALUE get_update_waveform_value(int stage,
    pixel_t old_pixel, pixel_t new_pixel)
{
    // TODO: should we also be using old_pixel to generate the waveform?

    const pixel_t base_color = WHITE;

    enum PIXEL_VALUE reset_val;
    enum PIXEL_VALUE write_val;
    if (base_color == WHITE) {
        reset_val = PV_WHITE;
        write_val = PV_BLACK;
    } else {
        reset_val = PV_BLACK;
        write_val = PV_WHITE;
    };

    const int diff_from_base = abs(new_pixel - base_color);

    // this one is easy
    if (stage == RESET_STAGE) {
        return reset_val;
    }

    // for other stages we need to figure out if this stage is needed
    // to hit write_time. write_time = diff_from_base * WRITE_TIME_UNIT_NS.

    bool should_write_now;
    if (stage <= RESET_STAGE - 1) {
        should_write_now = (diff_from_base <= stage);
    } else {
        should_write_now = (diff_from_base >= (stage - RESET_STAGE));
    }

    return should_write_now ? write_val : PV_NEUTRAL;
}
