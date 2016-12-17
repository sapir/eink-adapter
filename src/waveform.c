#include "waveform.h"


// http://stackoverflow.com/a/4415646
#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))


enum WAVEFORM {
    WF_W2W,
    WF_W2B,
    WF_B2W,
    WF_B2B,

    NUM_WAVEFORMS,
};

// TODO: unknown->white, unknown->black?
// TODO: does the old pixel value really matter?

struct waveform_stage {
    uint32_t ckv_high_delay;
    uint32_t ckv_low_delay;
    enum PIXEL_VALUE values[NUM_WAVEFORMS];
};

static const struct waveform_stage refresh_waveforms[] = {
//      (ns)   (ns)     (unused)     W->B        B->W       (unused)
    {  60*10, 60*40, { PV_NEUTRAL, PV_BLACK,  PV_WHITE,   PV_NEUTRAL } },
    {  60*60, 60*40, { PV_NEUTRAL, PV_WHITE,  PV_BLACK,   PV_NEUTRAL } },
    {  60*50, 60*40, { PV_NEUTRAL, PV_BLACK,  PV_WHITE,   PV_NEUTRAL } },

    // null stage to signify end of waveform
    {  0, 0, {} },
};

static const struct waveform_stage update_waveforms[] = {
//      (ns)   (ns)      W->W        W->B        B->W        B->B
    {  60*20, 60*40, { PV_BLACK,   PV_HIZ,     PV_HIZ,     PV_WHITE,   } },
    {  60*40, 60*40, { PV_BLACK,   PV_BLACK,   PV_WHITE,   PV_WHITE,   } },
    {  60*60, 60*40, { PV_WHITE,   PV_WHITE,   PV_BLACK,   PV_BLACK,   } },
    {  60*20, 60*40, { PV_HIZ,     PV_BLACK,   PV_WHITE,   PV_HIZ,     } },

    // null stage to signify end of waveform
    {  0, 0, {} },
};


void get_refresh_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns)
{
    if (stage < 0 || stage >= COUNT_OF(refresh_waveforms)) {
        *ckv_high_delay_ns = 0;
        *ckv_low_delay_ns = 0;
        return;
    }

    const struct waveform_stage *wstage = &refresh_waveforms[stage];
    *ckv_high_delay_ns = wstage->ckv_high_delay;
    *ckv_low_delay_ns = wstage->ckv_low_delay;
}

enum PIXEL_VALUE get_refresh_waveform_value(int stage, pixel_t pixel)
{
    const struct waveform_stage *wstage = &refresh_waveforms[stage];

    enum WAVEFORM wf_idx =
        (pixel == WHITE) ? WF_B2W : WF_W2B;

    return wstage->values[wf_idx];
}


void get_update_waveform_timings(int stage,
    uint32_t *ckv_high_delay_ns, uint32_t *ckv_low_delay_ns)
{
    if (stage < 0 || stage >= COUNT_OF(update_waveforms)) {
        *ckv_high_delay_ns = 0;
        *ckv_low_delay_ns = 0;
        return;
    }

    const struct waveform_stage *wstage = &update_waveforms[stage];
    *ckv_high_delay_ns = wstage->ckv_high_delay;
    *ckv_low_delay_ns = wstage->ckv_low_delay;
}

enum PIXEL_VALUE get_update_waveform_value(int stage,
    pixel_t old_pixel, pixel_t new_pixel)
{
    enum WAVEFORM wf_idx =
        (old_pixel == WHITE && new_pixel == WHITE) ? WF_W2W
        : (old_pixel == BLACK && new_pixel == WHITE) ? WF_B2W
        : (old_pixel == WHITE && new_pixel == BLACK) ? WF_W2B
        : WF_B2B;

    const struct waveform_stage *wstage = &update_waveforms[stage];
    return wstage->values[wf_idx];
}
