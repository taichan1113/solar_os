/* Test shim for the unmodified SDK encryption-initiate and result functions.
 * The runner appends those actual functions before the assertions below. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MYNEWT_VAL(name) 0
#define BLE_HS_LOG(...) ((void)0)
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_HS_EALREADY 2
#define BLE_HS_ENOMEM 6
#define BLE_HS_ENOTCONN 7
#define BLE_HS_HCI_ERR(x) ((x) ? 512+(x) : 0)
#define BLE_SM_IOACT_NONE 0
#define BLE_SM_PROC_STATE_NONE 255
#define BLE_SM_PROC_STATE_PAIR 0
#define BLE_SM_PROC_STATE_ENC_RESTORE 6
#define BLE_SM_PROC_STATE_ENC_START 5
#define BLE_SM_PROC_STATE_KEY_EXCH 7
#define BLE_SM_PROC_F_INITIATOR 1
#define BLE_SM_PROC_F_AUTHENTICATED 8
#define BLE_SM_PROC_F_BONDING 32
#define BLE_SM_ERR_UNSPECIFIED 8
#define BLE_HS_SM_US_ERR(x) (256+(x))
struct ble_sm_proc { uint16_t conn_handle; uint8_t key_size, state, flags, rx_key_flags; } slot;
struct ble_sm_result {
    int app_status, sm_err, execute, out_of_order, enc_cb, restore, bonded;
    void *state_arg;
    struct { int action; } passkey_params;
};
struct hci_start_encrypt {
    uint16_t connection_handle, encrypted_diversifier;
    uint64_t random_number;
    uint8_t long_term_key[16];
};
static struct ble_sm_proc *active;
static unsigned removes, frees, enc_events, hci_starts, timer_updates;
static bool allocation_fails;
static int encrypted, bonded, link_key_size;
static void ble_sm_update_sec_state(uint16_t conn, int enc, int auth, int bond, int key_size)
{ (void)conn; (void)auth; encrypted=enc; bonded=bond; link_key_size=key_size; }
static void ble_hs_lock(void) {}
static void ble_hs_unlock(void) {}
static struct ble_sm_proc *ble_sm_proc_find(uint16_t conn, int state, int initiator,
                                          struct ble_sm_proc **previous)
{ (void)state; (void)initiator; if(previous) *previous=NULL; return active && active->conn_handle==conn ? active : NULL; }
static void ble_sm_proc_remove(struct ble_sm_proc *proc, struct ble_sm_proc *previous)
{ (void)previous; assert(proc==active); active=NULL; ++removes; }
static void ble_sm_proc_free(struct ble_sm_proc *proc)
{ assert(proc!=active); ++frees; }
static void ble_sm_proc_set_timer(struct ble_sm_proc *proc)
{ assert(proc==active); ++timer_updates; }
static struct ble_sm_proc *ble_sm_proc_alloc(void)
{ if(allocation_fails) return NULL; assert(!active); memset(&slot,0,sizeof(slot)); return &slot; }
static void ble_sm_insert(struct ble_sm_proc *proc)
{ assert(!active); active=proc; }
static void ble_sm_exec(struct ble_sm_proc *proc, struct ble_sm_result *res, void *arg)
{ assert(proc==active && arg); ++hci_starts; res->execute=0; }
static void ble_sm_pair_fail_tx(uint16_t conn, int error) { (void)conn; (void)error; }
static void ble_sm_persist_keys(struct ble_sm_proc *proc) { (void)proc; }
static void ble_gap_pairing_complete_event(uint16_t conn, int error) { (void)conn; (void)error; }
static void ble_gap_enc_event(uint16_t conn, int status, int restore, int bonded)
{ (void)restore; (void)bonded; assert(conn==7 && status==0); ++enc_events; }
static void ble_gap_passkey_event(uint16_t conn, void *params) { (void)conn; (void)params; }

/* SDK_FUNCTIONS */

static void reset(void)
{
    active=NULL; removes=frees=enc_events=hci_starts=timer_updates=0; allocation_fails=false;
    encrypted=bonded=link_key_size=0;
    memset(&slot,0,sizeof(slot));
}
static void complete(void)
{
    assert(active);
    ble_sm_enc_event_rx(7,0,1);
    assert(!active && removes==1 && frees==1 && enc_events==1);
    assert(encrypted && bonded && link_key_size==16);
}
int main(int argc, char **argv)
{
    (void)argv;
    uint8_t dummy_key[16]={0};
    if(argc>1) {
        reset(); slot=(struct ble_sm_proc){.conn_handle=7,.key_size=16,.state=BLE_SM_PROC_STATE_ENC_RESTORE,.flags=BLE_SM_PROC_F_INITIATOR};
        active=&slot;
        assert(ble_sm_enc_initiate(7,16,dummy_key,0,0,0)==BLE_HS_EALREADY);
        ble_sm_enc_event_rx(7,0,1);
        printf("HCI encrypted=%d bonded=%d ENC_CHANGE callbacks=%u\n",encrypted,bonded,enc_events);
        fflush(stdout);
        assert(encrypted && bonded && enc_events==1);
        puts("actual SDK failure sequence repaired: PASS");
        return 0;
    }
    const int states[]={BLE_SM_PROC_STATE_ENC_RESTORE,BLE_SM_PROC_STATE_PAIR,BLE_SM_PROC_STATE_KEY_EXCH};
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        reset(); slot=(struct ble_sm_proc){.conn_handle=7,.key_size=16,.state=states[i],.flags=BLE_SM_PROC_F_INITIATOR};
        active=&slot;
        assert(ble_sm_enc_initiate(7,16,dummy_key,0,0,0)==BLE_HS_EALREADY);
        assert(active==&slot && slot.state==states[i]);
        assert(!removes && !frees && !enc_events && !hci_starts && !timer_updates);
        if(states[i]==BLE_SM_PROC_STATE_ENC_RESTORE) complete();
    }
    reset(); assert(!ble_sm_enc_initiate(7,16,dummy_key,0,0,1));
    assert(active && active->key_size==16 && hci_starts==1 && timer_updates==1);
    complete();
    reset(); allocation_fails=true;
    assert(ble_sm_enc_initiate(7,16,dummy_key,0,0,0)==BLE_HS_ENOMEM);
    assert(!active && !removes && !frees && !enc_events && !hci_starts);
    allocation_fails=false;
    assert(!ble_sm_enc_initiate(7,16,dummy_key,0,0,0)); complete();
    puts("actual SDK SM functions: EALREADY retains active procedure; completion, fresh start, OOM recovery: PASS");
}
