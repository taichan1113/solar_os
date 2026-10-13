#pragma once
/* Private RAM journal. Present requests are bounded before submission: at most
 * ten can be active, pending, replaced or completed, hence never an 11th fast
 * physical update. Initial Writer raster is established with OTP, not fast. */
#include "epd_ssd1677.h"
#include "solar_os_display_submission.h"
#include "solar_os_writer_burst_trial.h"

#define WQ_LIMIT 10U
#define WQ_INPUT_LIMIT 32U
#define WQ_FORMATION_CHARS 20U
#define WQ_FAST_INTERVAL_US 60000
#define WQ_OPTICAL_SETTLE_US 2000000
#define WQ_TICK_LIMIT 96U
typedef struct { int x, y, w, h; } wq_roi_t;
typedef struct {
    unsigned seq, ch, length;
    int64_t sent, accepted, mutated, request;
    uint64_t version;
    uint32_t document_hash;
} wq_input_t;
typedef struct {
    unsigned chars, length;
    int64_t doc_start, doc_end, render_start, render_end, submit_start, submit_end;
    uint64_t generation;
    uint32_t hash;
    uint64_t version;
    uint32_t document_hash;
    solar_os_display_submission_status_t status;
} wq_frame_t;
typedef struct {
    int64_t time;
    uint64_t version, rendered_version;
    bool pending, allowed, suspended, fault;
} wq_tick_t;
typedef struct {
    uint32_t hash, changed, fixed_changed, fixed_00, fixed_11, first_changed, first_00;
    uint32_t group_changed, group_00, group_11;
    int x0, y0, x1, y1;
    int64_t start, end, observer_us;
    esp_err_t error;
    epd_ssd1677_timing_t timing;
} wq_physical_t;
typedef struct {
    epd_ssd1677_t *driver;
    unsigned inputs, renders, starts, budget, following, interval_ms;
    bool armed, overflow, optical, video, aborted, at_gate;
    bool burst, coalesced;
    wq_burst_policy_t policy;
    /* Private event-plan seam for real app Host editing fixtures. Shell never
     * assigns this; live burst diagnostics retain the fixed 29-character plan. */
    const uint8_t *fixture_input;
    unsigned ticks;
    wq_tick_t tick[WQ_TICK_LIMIT];
    size_t initial_length;
    wq_roi_t fixed, first, group, signature;
    uint8_t *fixed_mask, *first_mask, *group_mask;
    uint8_t fixed_black_mask[30][32], first_black_mask[22][2];
    uint8_t group_black_mask[22][32];
    int64_t doc_start, doc_end, completed_at;
    wq_input_t input[WQ_INPUT_LIMIT];
    wq_frame_t frame[WQ_LIMIT];
    wq_physical_t physical[WQ_LIMIT];
} wq_trace_t;

static unsigned wq_budget(const wq_trace_t *q) { return q->budget ? q->budget : WQ_LIMIT; }
static unsigned wq_expected_char(const wq_trace_t *q)
{
    if (q->fixture_input) return q->fixture_input[q->inputs];
    if (!q->video) return q->inputs<WQ_LIMIT ? (unsigned)"abcdefghij"[q->inputs] : 0;
    if (q->inputs<WQ_FORMATION_CHARS) return 'a';
    const unsigned n=q->inputs-WQ_FORMATION_CHARS;
    return n<q->following ? (unsigned)"bcdefghij"[n] : 0;
}

static esp_err_t writer_quality_output_marker(const char *, size_t, void *);
static void writer_quality_render_begin(void);
static void writer_quality_render_end(solar_os_gfx_t *gfx, uint64_t *generation,
                                      esp_err_t *error);
static uint32_t writer_quality_document_hash(void);
static bool writer_quality_render_allowed(void);
