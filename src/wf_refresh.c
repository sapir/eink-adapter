#include "waveform.h"


#define REFRESH_CYCLES 1

void get_refresh_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns,
    uint32_t *stage_delay_us)
{
    if (stage < 0 || stage >= REFRESH_CYCLES*3) {
        *ckv_high_delay_ns = 0;
        *ckv_low_delay_ns = 0;
        *stage_delay_us = 0;
        return;
    }

    switch (stage % 3) {
    case 0: *ckv_high_delay_ns = 60*80; break;
    case 1: *ckv_high_delay_ns = 60*480; break;
    case 2: *ckv_high_delay_ns = 60*400; break;
    }

    *ckv_low_delay_ns = 60*80;
    *stage_delay_us = 250000;
}

enum PIXEL_VALUE get_refresh_waveform_value(int stage, pixel_t pixel)
{
    if (stage % 3 == 1) {
        return (pixel == WHITE) ? PV_BLACK : PV_WHITE;
    } else {
        return (pixel == WHITE) ? PV_WHITE : PV_BLACK;
    }
}
