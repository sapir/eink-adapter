#ifndef __WAVEFORM_H__
#define __WAVEFORM_H__


#ifdef __cplusplus
extern "C" {
#endif


#include "eink.h"


enum PIXEL_VALUE {
    PV_NEUTRAL = 0,
    PV_BLACK, // +15V
    PV_WHITE, // -15V
    PV_HIZ,
};


// get refresh waveform timings in nanoseconds at given stage. sets delays to
// 0 if there is no such stage.
void get_refresh_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns);

// get value at given stage of refresh waveform that clears everything to the
// given pixel. (this only works for full white/black)
enum PIXEL_VALUE get_refresh_waveform_value(int stage, pixel_t pixel);


// like get_refresh_waveform_timings, but for update waveforms
void get_update_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns);

// get value at given stage of update waveform that changes an old_p pixel
// to new_p.
enum PIXEL_VALUE get_update_waveform_value(int stage,
    pixel_t old_pixel, pixel_t new_pixel);


#ifdef __cplusplus
} // extern "C"
#endif


#endif
