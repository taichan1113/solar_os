/* Run the production worker with a deterministic scheduler and radio outcomes.
 * The real HID epoch/cancel/cleanup paths are covered separately by ble_hid_test. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef struct { uint8_t bda[6], addr_type; char name[32]; } ble_keyboard_peer_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_NOT_FOUND, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
enum { BLE_KEYBOARD_IDLE, BLE_KEYBOARD_SCANNING, BLE_KEYBOARD_CONNECTING,
       BLE_KEYBOARD_PASSKEY, BLE_KEYBOARD_FAILED, BLE_KEYBOARD_SCAN_RECONNECT };
#define BLE_KEYBOARD_RECONNECT_BACKOFF_INITIAL_MS 1000U
#define BLE_KEYBOARD_RECONNECT_INITIAL_DELAY_MS 250U
#define BLE_KEYBOARD_RECONNECT_BACKOFF_MAX_MS 5000U
#define SOLAR_OS_BLE_ADDRESS_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define SOLAR_OS_BLE_ADDRESS_ARGS(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define tskNO_AFFINITY 0
#define SOLAR_OS_TASK_ROLE_SYSTEM 0
#define SOLAR_OS_LOGI(...) ((void)0)
#define SOLAR_OS_LOGW(...) ((void)0)
#define SOLAR_OS_LOGD(...) ((void)0)
static bool initialized, connected, reconnect_stop_requested, reconnect_open_in_progress;
static bool reconnect_task_starting, reentrant_create;
static bool pairing_retry_pending, suppressed, have_peer, cancel_open, retirement_race;
static int state, reconnect_task_lock;
static TaskHandle_t reconnect_task_handle, scan_task_handle;
static uint32_t reconnect_attempt;
static ble_keyboard_peer_t peer = {.bda={1,2,3,4,5,6}, .addr_type=1, .name="remembered"};
static unsigned calls, creates, notifications, deletes, lock_after_open, waiting_count;
static uint32_t waits[64];
static esp_err_t outcomes[32];
static size_t outcome_count;
static void (*scheduled)(void *);
static void *scheduled_arg;
static void schedule_reconnect(uint32_t delay_ms);

static void enter(int *lock)
{
    (void)lock;
    if (retirement_race && calls > 0 && ++lock_after_open == 2) {
        /* CLOSE arrives after the successful-open break, before handle clear. */
        retirement_race = false;
        connected = false;
        state = BLE_KEYBOARD_IDLE;
        schedule_reconnect(250);
    }
}
#define portENTER_CRITICAL(x) enter(x)
#define portEXIT_CRITICAL(x) ((void)(x))
static size_t remembered_peer_count(void) { return have_peer ? 1 : 0; }
static const ble_keyboard_peer_t *primary_remembered_peer(void) { return have_peer ? &peer : NULL; }
static bool reconnect_is_suppressed(void) { return suppressed; }
static bool forget_operation_pending(void) { return false; }
static esp_err_t complete_deferred_bond_forget(void) { return ESP_OK; }
static esp_err_t start_pairing_scan_now(void) { assert(false); return ESP_FAIL; }
static void solar_os_task_delete_internal(TaskHandle_t task) { assert(!task); ++deletes; }
static void xTaskNotifyGive(TaskHandle_t task) { assert(task); ++notifications; }
static uint32_t ulTaskNotifyTake(int clear, uint32_t wait)
{ assert(clear); assert(waiting_count < 64); waits[waiting_count++] = wait; return 0; }
static esp_err_t scan_and_open_keyboard(int mode)
{
    assert(mode == BLE_KEYBOARD_SCAN_RECONNECT && reconnect_open_in_progress);
    assert(calls < outcome_count);
    const esp_err_t result = outcomes[calls++];
    state = result == ESP_OK ? BLE_KEYBOARD_IDLE : BLE_KEYBOARD_FAILED;
    connected = result == ESP_OK;
    if (cancel_open) { reconnect_stop_requested = true; suppressed = true; }
    return result;
}
static int solar_os_task_create_pinned_internal(void (*fn)(void *), const char *name,
    unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle, int core, int role)
{
    (void)name; (void)stack; (void)priority; (void)core; (void)role;
    if (reentrant_create) { reentrant_create=false; schedule_reconnect(0); }
    assert(!*handle); ++creates; *handle=(void *)(uintptr_t)creates;
    scheduled=fn; scheduled_arg=arg; return pdPASS;
}
#ifdef SOLAR_OS_RECONNECT_BASELINE_TEST
#include "../../.research/reconnect-before/worker.inc"
#else
#include "../../src/services/solar_os_ble_keyboard_reconnect.inc"
#endif

static void reset(void)
{
    initialized=have_peer=true;
    connected=reconnect_stop_requested=reconnect_open_in_progress=false;
    pairing_retry_pending=suppressed=cancel_open=retirement_race=false;
    reconnect_task_starting=reentrant_create=false;
    reconnect_task_handle=scan_task_handle=NULL;
    state=BLE_KEYBOARD_IDLE;
    calls=creates=notifications=deletes=waiting_count=lock_after_open=0;
    reconnect_attempt=0; outcome_count=0; scheduled=NULL; scheduled_arg=NULL;
}
static void run(void)
{ assert(scheduled); void (*fn)(void *)=scheduled; void *arg=scheduled_arg; scheduled=NULL; fn(arg); }
static void retry_failure(esp_err_t result)
{
    reset(); outcomes[0]=result; outcomes[1]=ESP_OK; outcome_count=2;
    schedule_reconnect(0); run();
    assert(calls==2 && connected && !reconnect_task_handle && !reconnect_open_in_progress);
    assert(waits[0]==2000 && creates==1 && deletes==1);
}
int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) {
        reset(); outcomes[0]=ESP_OK; outcome_count=1;
        schedule_reconnect(0); retirement_race=true; run();
        assert(!connected && reconnect_task_handle && scheduled);
        puts("disconnect during worker retirement retains reconnect ownership: ok");
        return 0;
    }
    reset(); have_peer=false; schedule_reconnect(0); assert(!creates);
    reset();
    for (size_t i=0;i<20;i++) outcomes[i]=ESP_ERR_NOT_FOUND;
    outcomes[20]=ESP_OK; outcome_count=21;
    schedule_reconnect(0); run();
    assert(calls==21 && connected && !reconnect_task_handle && creates==1);
    assert(waits[0]==2000 && waits[1]==4000 && waits[2]==5000 && waits[19]==5000);
    retry_failure(ESP_ERR_TIMEOUT); /* scan or connect timeout */
    retry_failure(ESP_FAIL);        /* HID open failure */
    reset(); outcomes[0]=ESP_OK; outcome_count=1; schedule_reconnect(0); run();
    connected=false; state=BLE_KEYBOARD_IDLE;
    outcomes[1]=ESP_OK; outcome_count=2; schedule_reconnect(250); run();
    assert(calls==2 && connected && creates==2 && waits[0]==250);
    reset(); cancel_open=true; outcomes[0]=ESP_ERR_INVALID_STATE; outcome_count=1;
    schedule_reconnect(0); run();
    assert(!reconnect_task_handle && !reconnect_open_in_progress && !reconnect_stop_requested);
    assert(!waiting_count && !connected); schedule_reconnect(0); assert(creates==1);
    /* Resume re-enables scheduling; repeated requests notify one worker. */
    suppressed=false; cancel_open=false; outcomes[1]=ESP_OK; outcome_count=2;
    schedule_reconnect(0); schedule_reconnect(0);
    assert(creates==2 && notifications==1); run(); assert(connected);
    reset(); suppressed=true; schedule_reconnect(0); assert(!creates);
    reset(); reentrant_create=true; schedule_reconnect(0); assert(creates==1);
    puts("production keyboard worker: absence, late appearance, failures, disconnect, cancel, suspend/resume: ok");
}
