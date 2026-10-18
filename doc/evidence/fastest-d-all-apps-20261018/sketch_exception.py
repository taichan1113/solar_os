from pathlib import Path
base=Path(__file__).resolve().parent;wt=base/'worktree'
def edit(name,old,new):
 p=wt/name;s=p.read_text(encoding='utf-8');assert s.count(old)==1,(name,s.count(old),old)
 p.write_text(s.replace(old,new),encoding='utf-8')
edit('src/services/solar_os_writer_lut_trial.h','    bool is_writer;',
     '    bool is_writer, is_sketch; /* Immutable source identity; Driver owns LUT policy. */')
edit('src/drivers/epd_ssd1677_writer_d.inc',
     '    if (t->baseline_selected || atomic_load(&writer_d_failed) ||',
     '    if (t->origin.is_sketch || t->baseline_selected || atomic_load(&writer_d_failed) ||')
edit('src/shell/solar_os_shell_writer_lut.inc',
     'scope=all-apps attempts=', 'scope=all-apps-except-sketch attempts=')
edit('src/shell/solar_os_shell_writer_lut.inc',
     'r->origin.is_writer ? "writer" : "other",',
     'r->origin.is_sketch ? "sketch" : r->origin.is_writer ? "writer" : "other",')
edit('src/shell/solar_os_shell_commands.c',
     'All apps default to D on every fastest update; baseline/disable selects A globally; no cumulative limit',
     'All apps except Sketch default to D; Sketch uses A; baseline/disable selects A globally; no cumulative limit')
for file in ('src/services/solar_os_display.c','src/services/solar_os_display_async.c'):
 edit(file,'if (!origin || !origin->is_writer) return ESP_ERR_INVALID_ARG;',
      'if (!origin || (!origin->is_writer && !origin->is_sketch)) return ESP_ERR_INVALID_ARG;')
edit('src/services/solar_os_gfx.h',
     'esp_err_t solar_os_gfx_submit_writer(solar_os_gfx_t *, uint64_t *, const solar_os_writer_frame_t *);',
     'esp_err_t solar_os_gfx_submit_writer(solar_os_gfx_t *, uint64_t *, const solar_os_writer_frame_t *);\nesp_err_t solar_os_gfx_submit_sketch(solar_os_gfx_t *, uint64_t *);')
edit('src/services/solar_os_gfx.c',
     'esp_err_t solar_os_gfx_submit_writer(solar_os_gfx_t *gfx, uint64_t *generation,',
     '''esp_err_t solar_os_gfx_submit_sketch(solar_os_gfx_t *gfx, uint64_t *generation)
{
    const solar_os_writer_frame_t origin = {.is_sketch = true};
    return solar_os_gfx_submit_writer(gfx, generation, &origin);
}

esp_err_t solar_os_gfx_submit_writer(solar_os_gfx_t *gfx, uint64_t *generation,''')
# Reuse the immutable origin path. Do not silently lose the Sketch identity
# in fallback paths; the normal PaperMono Executor supports tagged snapshots.
edit('src/services/solar_os_display.c',
     '    if (err == ESP_ERR_NOT_SUPPORTED) {\n        solar_os_display_present(u8g2, mode);\n        return ESP_OK;\n    }\n    if (err != ESP_OK) return err;\n    display_target_slot_t *slot',
     '''    if (err == ESP_ERR_NOT_SUPPORTED) {
#if SOLAR_OS_SSD1677_WRITER_D
        if (origin && origin->is_sketch) return err;
#endif
        solar_os_display_present(u8g2, mode);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    display_target_slot_t *slot''')
edit('src/services/solar_os_display.c',
     '        display_release_slot_ref(index, generation);\n        solar_os_display_present(u8g2, mode);\n        return ESP_OK;\n    }\n    if (blocked) err',
     '''        display_release_slot_ref(index, generation);
#if SOLAR_OS_SSD1677_WRITER_D
        if (origin && origin->is_sketch) return ESP_ERR_INVALID_STATE;
#endif
        solar_os_display_present(u8g2, mode);
        return ESP_OK;
    }
    if (blocked) err''')
edit('src/services/solar_os_display.c',
     '        if (origin && submission.submit_writer)',
     '        if (origin && origin->is_sketch && !submission.submit_writer) err = ESP_ERR_NOT_SUPPORTED;\n        else if (origin && submission.submit_writer)')
f='src/apps/solar_os_sketch.c';p=wt/f;s=p.read_text(encoding='utf-8')
s=s.replace('solar_os_gfx_submit(gfx,', 'sketch_submit(gfx,')
point=s.index('static bool sketch_render_paint(')
s=s[:point]+'''/* Source identity travels with each submitted snapshot. The SSD1677
 * Driver keeps Sketch on Baseline; this does not toggle the global D mode. */
static esp_err_t sketch_submit(solar_os_gfx_t *gfx, uint64_t *generation)
{
#if SOLAR_OS_SSD1677_WRITER_D
    return solar_os_gfx_submit_sketch(gfx, generation);
#else
    return solar_os_gfx_submit(gfx, generation);
#endif
}

'''+s[point:]
p.write_text(s,encoding='utf-8')
f='README.md';p=wt/f;s=p.read_text(encoding='utf-8')
s=s.replace('including Shell, Launcher, Writer, Sketch and\nTypeMouse. App origin tags are diagnostic only.',
'''including Shell, Launcher, Writer and TypeMouse. Sketch is temporarily
EXCLUDED and uses Baseline A because strokes did not become black in the
first all-app hardware check. Its source identity travels with the snapshot;
the Driver owns this exception. Other origin tags are diagnostic only.''')
s=s.replace('selects D globally again;', 'selects D again (Sketch remains Baseline);')
s=s.replace('now with the SAME GLOBAL scope.', 'now with the SAME GLOBAL scope and Sketch exception.')
p.write_text(s,encoding='utf-8')
print('Sketch-only Baseline exception implemented via existing immutable origin path')
