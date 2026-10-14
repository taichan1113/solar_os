/* Lifecycle/context adapter, compiled as part of solar_os_sessions.c. */
static uint64_t session_touch_bound_lifetime[SOLAR_OS_SESSION_MAX];
static uint64_t session_touch_legacy_bound_lifetime;

static bool session_touch_deliver(void *identity, const solar_os_event_t *event)
{
    solar_os_session_entry_t *session = identity;
    const bool legacy = session == NULL;
    const solar_os_app_t *app = legacy ? session_state.foreground_app : session->app;
    const uint64_t lifetime = legacy ? session_state.legacy_touch_lifetime : session->touch_lifetime;
    if (!lifetime || (legacy ? session_touch_legacy_bound_lifetime != lifetime :
        session->id >= SOLAR_OS_SESSION_MAX || session_touch_bound_lifetime[session->id] != lifetime)) return false;
    if (!app || !app->event || (legacy && session_state.foreground_session != NULL) ||
        (!legacy && (!session->used || !session->started || session->suspended))) return false;
    const bool cancel = event->type == SOLAR_OS_EVENT_TOUCH_CANCEL ||
        (event->type == SOLAR_OS_EVENT_TOUCH_FRAME && event->data.touch_frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL);
    if (!cancel) {
        if (!legacy) return solar_os_sessions_dispatch_session_event(session->id, event);
        solar_os_sessions_dispatch_foreground_event(event);
        return true;
    }
    /* Cancellation is synchronous BEFORE lifecycle callbacks/free. Do not process
     * new app requests recursively during this control notification. */
    solar_os_session_context_snapshot_t previous = {0};
    solar_os_session_entry_t *previous_foreground = session_state.foreground_session;
    const uint64_t previous_lifetime = previous_foreground ? previous_foreground->touch_lifetime : session_state.legacy_touch_lifetime;
    const solar_os_app_t *previous_app = session_state.foreground_app;
    session_context_capture(&previous);
    if (!legacy) session_prepare_context(session);
    (void)app->event(session_state.ctx, event);
    /* Unmodified legacy apps do not know TOUCH_CANCEL. Their existing balanced
     * suspend/resume callbacks clear gestures without inventing a RELEASE/click. */
    const bool live = legacy ? session_state.foreground_app == app && session_state.legacy_touch_lifetime == lifetime :
        session->used && session->started && !session->suspended && session->app == app && session->touch_lifetime == lifetime;
    if (live && event->type == SOLAR_OS_EVENT_TOUCH_CANCEL && app->suspend && app->resume) {
        app->suspend(session_state.ctx);
        if (legacy ? session_state.foreground_app == app && session_state.legacy_touch_lifetime == lifetime :
            session->used && session->started && !session->suspended && session->app == app && session->touch_lifetime == lifetime)
            app->resume(session_state.ctx);
    }
    if (session_state.foreground_session == previous_foreground &&
        (previous_foreground ? previous_foreground->used && previous_foreground->touch_lifetime == previous_lifetime :
            session_state.foreground_app == previous_app && session_state.legacy_touch_lifetime == previous_lifetime))
        session_context_restore(&previous);
    else if (session_state.foreground_session) restore_foreground_context();
    else session_restore_base_context();
    return true;
}

void solar_os_sessions_refresh_touch_consumers(void)
{
    for (size_t i = 0; i < SOLAR_OS_SESSION_MAX; ++i) {
        solar_os_session_entry_t *s = &session_state.sessions[i];
        if (session_touch_bound_lifetime[i] != s->touch_lifetime) {
            solar_os_sessions_touch_unbind((uint8_t)i);
            session_touch_bound_lifetime[i] = s->touch_lifetime;
        }
        if (s->used && s->started && !s->suspended && s->touch_lifetime && s->app && s->app->event && s->display_target[0])
            (void)solar_os_sessions_touch_bind(s->id, s->display_target, s->app, s, session_touch_deliver);
        else solar_os_sessions_touch_unbind((uint8_t)i);
    }
    if (!session_state.foreground_session && session_state.foreground_app && session_state.legacy_touch_lifetime) {
        if (session_touch_legacy_bound_lifetime != session_state.legacy_touch_lifetime) {
            solar_os_sessions_touch_unbind(UINT8_MAX);
            session_touch_legacy_bound_lifetime = session_state.legacy_touch_lifetime;
        }
        char target[SOLAR_OS_DISPLAY_TARGET_NAME_MAX];
        if (!session_copy_input_focus(target, sizeof(target)))
            strlcpy(target, session_state.builtin_display_target, sizeof(target));
        if (target[0]) (void)solar_os_sessions_touch_bind(UINT8_MAX, target,
            session_state.foreground_app, NULL, session_touch_deliver);
    } else solar_os_sessions_touch_unbind(UINT8_MAX);
}

void solar_os_sessions_quiesce_touch_consumers(void)
{ solar_os_sessions_touch_unbind_all(); }

bool solar_os_sessions_dispatch_touch_frame(const solar_os_input_touch_frame_t *frame)
{
    if (!frame) return false;
    if (frame->state == SOLAR_OS_INPUT_TOUCH_CANCEL)
        return solar_os_sessions_touch_dispatch(frame, 1, 1, 0);
    solar_os_display_target_t target;
    solar_os_terminal_profile_t profile;
    if (!solar_os_display_find_target(frame->target, &target) ||
        solar_os_display_get_terminal_profile(frame->target, &profile) != ESP_OK) return false;
    return solar_os_sessions_touch_dispatch(frame, target.width, target.height, profile.orientation_degrees);
}
