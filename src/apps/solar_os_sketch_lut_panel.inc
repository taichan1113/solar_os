/* Opt-in PaperMono standalone trial controls. Hardware operations use the same
 * executor fences and Driver controls as USB; no shell input or NVS writes.
 * Panel/initial-canvas presents are drained OUTSIDE the measured interval. */
static const char *sketch_lut_name(unsigned mode)
{
    return mode == 2 ? "E1" : mode == 1 ? "D" : "A";
}

static esp_err_t sketch_lut_target(solar_os_display_target_t *target)
{
    if (!solar_os_display_find_target("display0", target) || !target->ready ||
        !target->u8g2 || strcmp(target->driver, "ssd1677") ||
        !target->submission.wait) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

static esp_err_t sketch_lut_control(const char *mode,
                                   epd_ssd1677_writer_status_t *status)
{
    solar_os_display_target_t target;
    const esp_err_t err = sketch_lut_target(&target);
    if (err != ESP_OK) return err;
    epd_ssd1677_t *driver = (epd_ssd1677_t *)
        ((uint8_t *)target.u8g2 - offsetof(epd_ssd1677_t, u8g2));
    return epd_ssd1677_writer_lut(driver, mode, status, NULL);
}

static esp_err_t sketch_lut_drain(void)
{
    solar_os_display_target_t target;
    const esp_err_t err = sketch_lut_target(&target);
    if (err != ESP_OK) return err;
    return target.submission.wait(target.submission.context, 10000);
}

/* Always restore ordinary D / Sketch A on stop, suspend or exit. Does not
 * redraw, clean the panel, reset the Driver, or discard the document. */
static void sketch_lut_end(void)
{
    if (!sketch_state_storage) return;
    esp_err_t err = sketch_lut_drain();
    if (sketch.lut_running) {
        const esp_err_t stopped = sketch_lut_control("trial-stop", &sketch.lut_result);
        if (err == ESP_OK) err = stopped;
        sketch.lut_running = false;
    }
    const esp_err_t restored = sketch_lut_control("candidate_d", NULL);
    if (err == ESP_OK) err = restored;
    if (restored == ESP_OK) sketch.lut_actual = 0;
    sketch.lut_check_pending = false;
    if (err != ESP_OK) sketch.lut_error = err;
}

static void sketch_lut_open(solar_os_context_t *ctx)
{
    /* Finish the last release/preview before excluding subsequent panel UI. */
    if (sketch.render_pending) sketch_render(ctx);
    sketch_lut_end();
    sketch.drawing = false;
    sketch.modal = SKETCH_MODAL_LUT;
    sketch_request_full();
}

static void sketch_lut_start(solar_os_context_t *ctx)
{
    static const char *const settings[] = {
        "refresh=fastest", "fastest-path=tile", "dirty-window=rows-dual", "spi-mhz=20"
    };
    static const char *const modes[] = {"baseline", "candidate_d_sketch", "candidate_e1"};
    sketch.lut_error = ESP_OK;
    epd_ssd1677_writer_status_t before = {0};
    esp_err_t err = sketch_lut_drain();
    for (unsigned i = 0; err == ESP_OK && i < sizeof(settings)/sizeof(settings[0]); ++i)
        err = solar_os_display_set_controller_mode("display0", settings[i]);
    if (err == ESP_OK) err = sketch_lut_control("baseline", &before);
    if (err == ESP_OK && (before.failed || before.driver_errors || before.trial.active))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) err = solar_os_display_set_controller_mode("display0", "refresh=cleanup");
    if (err != ESP_OK) goto failed;

    /* Reached only after the explicit destructive confirmation. Detach the
     * filename so later Save cannot overwrite a previously opened PNG. */
    solar_os_sketch_canvas_clear(&sketch.canvas, 0U);
    sketch.path[0] = '\0';
    sketch.dirty = false;
    sketch.drawing = false;
    sketch.tool = SKETCH_TOOL_FREEHAND;
    sketch.color = 3U;
    sketch.pattern = 0U;
    sketch.weight = 2U;
    sketch.modal = SKETCH_MODAL_NONE;
    sketch.lut_actual = sketch.lut_selected;
    /* The initial header says Stop; its present must precede trial-start. */
    sketch.lut_running = true;
    sketch_request_full();
    sketch_render(ctx);
    sketch.lut_running = false;
    if (sketch.render_pending) { err = ESP_FAIL; goto failed; }
    err = sketch_lut_drain();
    if (err != ESP_OK) goto failed;
    /* Check that cleanup was really transmitted, not merely requested. */
    epd_ssd1677_writer_status_t status = {0};
    err = sketch_lut_control("status", &status);
    if (err != ESP_OK || status.failed || status.driver_errors ||
        status.otp_physical < before.otp_physical ||
        status.otp_physical - before.otp_physical != 2 ||
        status.records < before.records || status.records - before.records != 1) {
        if (err == ESP_OK) err = ESP_ERR_INVALID_STATE;
        goto failed;
    }
    err = sketch_lut_control(modes[sketch.lut_selected], NULL);
    if (err == ESP_OK) err = sketch_lut_control("trial-start", &sketch.lut_result);
    if (err != ESP_OK) goto failed;
    sketch.lut_result_mode = sketch.lut_selected;
    sketch.lut_running = true;
    sketch.lut_check_pending = false;
    return;
failed:
    sketch.lut_error = err;
    sketch.lut_running = false;
    sketch_lut_end();
    sketch.modal = SKETCH_MODAL_LUT;
    sketch_request_full();
}

/* Check only after RELEASE, never on every update or while a stroke is being
 * drawn. A single long stroke can exceed 20; do not claim a hard frame limit. */
static void sketch_lut_check(solar_os_context_t *ctx)
{
    if (!sketch.lut_running || !sketch.lut_check_pending) return;
    sketch.lut_check_pending = false;
    esp_err_t err = sketch_lut_drain();
    if (err == ESP_OK) err = sketch_lut_control("trial-status", &sketch.lut_result);
    if (err != ESP_OK || sketch.lut_result.trial.physical >= 20 ||
        sketch.lut_result.trial.errors || sketch.lut_result.failed) {
        if (err != ESP_OK) sketch.lut_error = err;
        sketch_lut_open(ctx);
    }
}

static int sketch_lut_button_y(const sketch_layout_t *layout, unsigned row)
{
    return layout->screen_height - 156 + (int)row * 46;
}

static void sketch_lut_render(solar_os_gfx_t *gfx, const sketch_layout_t *layout)
{
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_fill_rect(gfx, 8, 8, layout->screen_width - 16, layout->screen_height - 16);
    solar_os_gfx_set_color(gfx, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_rect(gfx, 8, 8, layout->screen_width - 16, layout->screen_height - 16);
    solar_os_gfx_set_font(gfx, SOLAR_OS_GFX_FONT_SMALL);
    solar_os_gfx_text(gfx, 20, 30, "Sketch A / D / E1 trial");
    if (sketch.modal == SKETCH_MODAL_LUT_CONFIRM) {
        char text[80];
        snprintf(text, sizeof(text), "Start %s with an empty canvas?", sketch_lut_name(sketch.lut_selected));
        solar_os_gfx_text(gfx, 20, 62, text);
        solar_os_gfx_text(gfx, 20, 86, "Unsaved canvas will be cleared.");
        solar_os_gfx_text(gfx, 20, 110, "Saved PNG files are kept.");
        sketch_draw_button(gfx, 20, sketch_lut_button_y(layout, 1), layout->screen_width - 40,
                           40, "Clear + Start", false);
        sketch_draw_button(gfx, 20, sketch_lut_button_y(layout, 2), layout->screen_width - 40,
                           40, "Cancel", false);
        return;
    }
    char text[96];
    const epd_ssd1677_trial_summary_t *s = &sketch.lut_result.trial;
    snprintf(text, sizeof(text), "Last %s: %llu updates, %llu errors",
             sketch_lut_name(sketch.lut_result_mode),
             (unsigned long long)s->physical, (unsigned long long)s->errors);
    solar_os_gfx_text(gfx, 20, 58, text);
    snprintf(text, sizeof(text), "A:%llu D:%llu E1:%llu OTP:%llu",
             (unsigned long long)s->a, (unsigned long long)s->d,
             (unsigned long long)s->e1, (unsigned long long)s->otp);
    solar_os_gfx_text(gfx, 20, 82, text);
    solar_os_gfx_text(gfx, 20, 106, "Draw short strokes. Tap Stop to inspect.");
    solar_os_gfx_text(gfx, 20, 130, "Auto-stop at stroke end if updates >= 20.");
    if (sketch.lut_error != ESP_OK) {
        snprintf(text, sizeof(text), "Control error: %d. Trial stopped.", (int)sketch.lut_error);
        solar_os_gfx_text(gfx, 20, 154, text);
    }
    const int width = (layout->screen_width - 48) / 3;
    for (unsigned i = 0; i < 3; ++i)
        sketch_draw_button(gfx, 20 + (int)i * (width + 4), sketch_lut_button_y(layout, 0),
                           width, 40, sketch_lut_name(i), sketch.lut_selected == i);
    sketch_draw_button(gfx, 20, sketch_lut_button_y(layout, 1), layout->screen_width - 40,
                       40, "New trial...", false);
    sketch_draw_button(gfx, 20, sketch_lut_button_y(layout, 2), layout->screen_width - 40,
                       40, "Normal / Back", false);
}

static void sketch_lut_pointer(solar_os_context_t *ctx, const sketch_layout_t *layout)
{
    const int x = sketch.pointer_x, y = sketch.pointer_y;
    if (x < 20 || x >= layout->screen_width - 20) return;
    for (unsigned row = 0; row < 3; ++row) {
        const int top = sketch_lut_button_y(layout, row);
        if (y < top || y >= top + 40) continue;
        if (sketch.modal == SKETCH_MODAL_LUT_CONFIRM) {
            if (row == 1) sketch_lut_start(ctx);
            else if (row == 2) { sketch.modal = SKETCH_MODAL_LUT; sketch_request_full(); }
        } else if (row == 0) {
            const int width = (layout->screen_width - 48) / 3;
            const int column = (x - 20) / (width + 4);
            if (column < 3 && (x - 20) % (width + 4) < width) {
                sketch.lut_selected = (unsigned)column;
                sketch_request_full();
            }
        } else if (row == 1) {
            sketch.modal = SKETCH_MODAL_LUT_CONFIRM;
            sketch_request_full();
        } else {
            sketch_lut_end();
            sketch.modal = SKETCH_MODAL_NONE;
            sketch_request_full();
        }
        return;
    }
}
