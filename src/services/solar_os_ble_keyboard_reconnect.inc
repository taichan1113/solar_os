/* Internal keyboard reconnect worker; included by the backend and host tests. */
static void reconnect_task(void *arg)
{
    const uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    uint32_t backoff_ms = BLE_KEYBOARD_RECONNECT_BACKOFF_INITIAL_MS;

    if (delay_ms > 0) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }

    while (initialized && remembered_peer_count() > 0 && !connected && !reconnect_is_suppressed()) {
        portENTER_CRITICAL(&reconnect_task_lock);
        const bool stop_requested = reconnect_stop_requested;
        portEXIT_CRITICAL(&reconnect_task_lock);
        if (stop_requested) {
            break;
        }

        if (scan_task_handle == NULL &&
            state != BLE_KEYBOARD_SCANNING &&
            state != BLE_KEYBOARD_CONNECTING &&
            state != BLE_KEYBOARD_PASSKEY) {
            const ble_keyboard_peer_t *peer = primary_remembered_peer();
            if (peer == NULL) {
                break;
            }
            portENTER_CRITICAL(&reconnect_task_lock);
            if (reconnect_stop_requested) {
                portEXIT_CRITICAL(&reconnect_task_lock);
                break;
            }
            reconnect_open_in_progress = true;
            portEXIT_CRITICAL(&reconnect_task_lock);

            ++reconnect_attempt;
            SOLAR_OS_LOGD(TAG, "attempt=%" PRIu32 " peer=" SOLAR_OS_BLE_ADDRESS_FMT
                          " type=%u name=%s", reconnect_attempt,
                          SOLAR_OS_BLE_ADDRESS_ARGS(peer->bda), peer->addr_type, peer->name);

            SOLAR_OS_LOGI(TAG,
                          "reconnecting remembered keyboard " SOLAR_OS_BLE_ADDRESS_FMT,
                          SOLAR_OS_BLE_ADDRESS_ARGS(peer->bda));
            const esp_err_t ret = scan_and_open_keyboard(BLE_KEYBOARD_SCAN_RECONNECT);

            portENTER_CRITICAL(&reconnect_task_lock);
            reconnect_open_in_progress = false;
            const bool stop_after_open = reconnect_stop_requested;
            portEXIT_CRITICAL(&reconnect_task_lock);
            if (ret == ESP_OK) {
                backoff_ms = BLE_KEYBOARD_RECONNECT_BACKOFF_INITIAL_MS;
            } else if (ret != ESP_ERR_NOT_FOUND) {
                SOLAR_OS_LOGI(TAG,
                              "reconnect open failed; returning to scan: %s",
                              esp_err_to_name(ret));
            }
            if (stop_after_open) {
                break;
            }
            if (connected) {
                break;
            }

            if (backoff_ms < BLE_KEYBOARD_RECONNECT_BACKOFF_MAX_MS) {
                backoff_ms *= 2U;
                if (backoff_ms > BLE_KEYBOARD_RECONNECT_BACKOFF_MAX_MS) {
                    backoff_ms = BLE_KEYBOARD_RECONNECT_BACKOFF_MAX_MS;
                }
            }

            SOLAR_OS_LOGD(TAG, "attempt=%" PRIu32 " retry result=%s backoff_ms=%" PRIu32,
                          reconnect_attempt, esp_err_to_name(ret), backoff_ms);

            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms));
            continue;
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }

    portENTER_CRITICAL(&reconnect_task_lock);
    const bool stopped = reconnect_stop_requested;
    reconnect_task_handle = NULL;
    reconnect_stop_requested = false;
    reconnect_open_in_progress = false;
    portEXIT_CRITICAL(&reconnect_task_lock);

    SOLAR_OS_LOGD(TAG, "attempt=%" PRIu32 " task exit initialized=%u connected=%u"
                  " remembered=%u suppressed=%u", reconnect_attempt, initialized,
                  connected, (unsigned)remembered_peer_count(), reconnect_is_suppressed());

    (void)complete_deferred_bond_forget();
    if (pairing_retry_pending && !forget_operation_pending()) {
        const esp_err_t pair_ret = start_pairing_scan_now();
        if (pair_ret != ESP_OK) {
            SOLAR_OS_LOGW(TAG,
                          "deferred pairing start after reconnect stop failed: %s",
                          esp_err_to_name(pair_ret));
        }
    }
    /* CLOSE may notify this worker after its successful-open break but before
     * its handle is cleared. Recheck after retiring ownership so that wakeup
     * cannot disappear with the old task. Scheduling still respects suppression. */
    if (!stopped) schedule_reconnect(BLE_KEYBOARD_RECONNECT_INITIAL_DELAY_MS);
    solar_os_task_delete_internal(NULL);
}

static void schedule_reconnect(uint32_t delay_ms)
{
    if (!initialized || connected || remembered_peer_count() == 0 || reconnect_is_suppressed()) {
        return;
    }
    portENTER_CRITICAL(&reconnect_task_lock);
    TaskHandle_t active_reconnect_task = reconnect_task_handle;
    const bool reconnect_stopping = reconnect_stop_requested;
    const bool starting = reconnect_task_starting;
    if (active_reconnect_task != NULL && !reconnect_stopping) {
        /* Notify before the owner can clear/delete this task handle. */
        xTaskNotifyGive(active_reconnect_task);
    }
    if (active_reconnect_task == NULL && !starting) {
        reconnect_task_starting = true;
        reconnect_stop_requested = false;
        reconnect_open_in_progress = false;
    }
    portEXIT_CRITICAL(&reconnect_task_lock);
    if (active_reconnect_task != NULL) {
        return;
    }
    if (starting) return;
    const BaseType_t created = solar_os_task_create_pinned_internal(reconnect_task,
                                             "ble_kbd_reconn",
                                             4096,
                                             (void *)(uintptr_t)delay_ms,
                                             3,
                                             &reconnect_task_handle,
                                             tskNO_AFFINITY,
                                             SOLAR_OS_TASK_ROLE_SYSTEM);
    if (created != pdPASS) {
        reconnect_task_handle = NULL;
        SOLAR_OS_LOGW(TAG, "reconnect task failed");
    }
    portENTER_CRITICAL(&reconnect_task_lock);
    reconnect_task_starting = false;
    const bool retired_during_create = created == pdPASS && reconnect_task_handle == NULL;
    portEXIT_CRITICAL(&reconnect_task_lock);
    if (retired_during_create) schedule_reconnect(delay_ms);
}

