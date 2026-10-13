#pragma once
/* Trial-only render permission. Input mutation and parsing never wait here.
 * The maximum burst deadline is anchored to the LAST SUBMISSION, not reset by
 * new characters. Silence permits an earlier trailing submission. */
#include <stdbool.h>
#include <stdint.h>
#define WQ_BURST_PERIOD_US 160000
#define WQ_BURST_IDLE_US 80000
typedef struct {
    int64_t last_input, last_render;
    uint64_t rendered_version;
    bool have_input, have_render, leading;
} wq_burst_policy_t;

static void wq_burst_input(wq_burst_policy_t *p, int64_t now)
{
    if (!p->have_input || now-p->last_input >= WQ_BURST_IDLE_US) p->leading=true;
    p->last_input=now; p->have_input=true;
}
static bool wq_burst_allowed(const wq_burst_policy_t *p, int64_t now)
{
    return !p->have_render || p->leading ||
           now-p->last_render >= WQ_BURST_PERIOD_US ||
           (p->have_input && now-p->last_input >= WQ_BURST_IDLE_US);
}
static void wq_burst_rendered(wq_burst_policy_t *p, int64_t now, uint64_t version)
{
    p->last_render=now; p->have_render=true; p->leading=false;
    p->rendered_version=version;
}
