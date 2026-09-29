#!/usr/bin/env python3
"""Exercise production BLE queue/timer functions with a deterministic host shim.

No radio emulation: SMP, HCI and GATT discovery still need hardware testing.
Only the OS/transport boundary is stubbed; function bodies come from the firmware.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[2] / "app/BLERepeaterBridge.cpp"
source = SOURCE.read_text()
mesh_source = SOURCE.with_name("RepeaterMesh.cpp").read_text()
assert "size_t reply_capacity = sender_timestamp ? CLI_REMOTE_REPLY_SIZE : CLI_REPLY_SIZE;" in mesh_source
assert "reply_capacity -= 3;" in mesh_source
assert "repeater_bridge_handle_command(command, reply, reply_capacity)" in mesh_source


def block(text, start):
    begin = text.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def definition(signature):
    return block(source, source.index(signature + "\n{"))


bridge_source = SOURCE.with_name("RepeaterBridge.cpp").read_text()
status_branch = block(bridge_source, bridge_source.index('if (strcmp(command, "bridge") == 0)'))


constants = source[source.index("constexpr uint32_t BRIDGE_MAGIC"):
                   source.index("#define ZEPHCORE_BRIDGE_SERVICE_UUID")]
records = source[source.index("struct __packed BridgeFrame"):
                 source.index("static RepeaterDataStore *s_store")]
globals_ = source[source.index("static RepeaterDataStore *s_store"):
                  source.index("static const uint8_t s_default_lmk")]

shim = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <algorithm>
#include <vector>
#define __packed __attribute__((packed))
#define K_MSGQ_DEFINE(name, ...) int name
#define K_SEM_DEFINE(name, ...) int name
#define ARG_UNUSED(x) (void)(x)
#define LOG_WRN(...)
#define LOG_INF(...)
#define LOG_ERR(...)
#define IS_ENABLED(x) (x)
#define CONFIG_SETTINGS 1
#define MAX(a,b) std::max(a,b)
using atomic_t = int32_t;
static int32_t atomic_get(const atomic_t *p) { return *p; }
static int32_t atomic_set(atomic_t *p, int32_t n) { auto old=*p; *p=n; return old; }
static int32_t atomic_inc(atomic_t *p) { return atomic_set(p, uint32_t(*p)+1); }
static void atomic_set_bit(atomic_t *p, unsigned bit) { *p |= 1U<<bit; }
struct k_spinlock {};
using k_spinlock_key_t = int;
static int k_spin_lock(k_spinlock *) { return 0; }
static void k_spin_unlock(k_spinlock *, int) {}
struct k_work {};
static int64_t now_ms = 1000;
static int64_t k_uptime_get() { return now_ms; }
static void k_sem_give(int *) {}
static unsigned rx_queued;
#define K_NO_WAIT 0
static int k_msgq_put(int *, const void *, int) {
    if (rx_queued==8) return -ENOMEM;
    ++rx_queued; return 0;
}
struct RepeaterBridgePrefs { uint8_t peer_mac[6]; uint8_t lmk[16]; uint8_t peer_addr_type; uint8_t forward_priority; };
static RepeaterBridgePrefs stored_prefs{};
struct RepeaterDataStore {
    bool loadBridgePrefs(RepeaterBridgePrefs &p) { p=stored_prefs; return true; }
    bool saveBridgePrefs(const RepeaterBridgePrefs &p) { stored_prefs=p; return true; }
};
static unsigned wakes;
namespace mesh { struct Dispatcher { void notifyWake() { ++wakes; } }; }
struct bt_conn {};
struct bt_addr_le_t { uint8_t type; struct { uint8_t val[6]; } a; };
struct bt_uuid { int value; };
struct { bt_uuid uuid; } bridge_service_uuid{{1}}, bridge_data_uuid{{2}};
static bt_uuid ccc_uuid{3};
#define BT_UUID_GATT_CCC (&ccc_uuid)
#define BT_GATT_ITER_STOP 0
#define BT_GATT_ITER_CONTINUE 1
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_SUBSCRIBE_FLAG_VOLATILE 0
#define BT_GATT_SUBSCRIBE_FLAG_NO_RESUB 1
#define BT_GATT_DISCOVER_PRIMARY 0
#define BT_GATT_DISCOVER_CHARACTERISTIC 1
#define BT_GATT_DISCOVER_DESCRIPTOR 2
#define BT_ATT_FIRST_ATTRIBUTE_HANDLE 1
#define BT_ATT_LAST_ATTRIBUTE_HANDLE 0xffff
struct bt_gatt_attr { const bt_uuid *uuid; uint16_t handle; const void *user_data; uint16_t value_handle; };
struct bt_gatt_service_val { uint16_t end_handle; };
struct bt_gatt_discover_params {
    const bt_uuid *uuid;
    uint8_t (*func)(bt_conn *,const bt_gatt_attr *,bt_gatt_discover_params *);
    uint16_t start_handle,end_handle; uint8_t type;
};
struct bt_gatt_subscribe_params {
    uint16_t value_handle,ccc_handle,value;
    uint8_t (*notify)(bt_conn *,bt_gatt_subscribe_params *,const void *,uint16_t);
    void (*subscribe)(bt_conn *,uint8_t,bt_gatt_subscribe_params *);
    int min_security; atomic_t flags[1];
};
static int bt_uuid_cmp(const bt_uuid *a,const bt_uuid *b) { return a->value-b->value; }
static uint16_t bt_gatt_attr_value_handle(const bt_gatt_attr *a) { return a->value_handle; }
static unsigned discovers,subscribes,security_requests;
static int discovery_error,subscribe_error,security_request_error;
static bt_gatt_discover_params *last_discovery;
static int bt_gatt_discover(bt_conn *,bt_gatt_discover_params *p) {
    ++discovers; last_discovery=p; return discovery_error;
}
static int bt_gatt_subscribe(bt_conn *,bt_gatt_subscribe_params *) { ++subscribes; return subscribe_error; }
using bt_security_t = int;
enum bt_security_err {
    BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_AUTH_FAIL, BT_SECURITY_ERR_PIN_OR_KEY_MISSING,
    BT_SECURITY_ERR_OOB_NOT_AVAILABLE, BT_SECURITY_ERR_AUTH_REQUIREMENT,
    BT_SECURITY_ERR_PAIR_NOT_SUPPORTED, BT_SECURITY_ERR_PAIR_NOT_ALLOWED,
    BT_SECURITY_ERR_INVALID_PARAM, BT_SECURITY_ERR_KEY_REJECTED, BT_SECURITY_ERR_UNSPECIFIED
};
static int bt_conn_set_security(bt_conn *,int) { ++security_requests; return security_request_error; }
static bt_conn fake_conn;
static bt_addr_le_t fake_peer;
static const bt_addr_le_t *bt_conn_get_dst(const bt_conn *) { return &fake_peer; }
static int security_level=2;
#define BT_SECURITY_L2 2
static int bt_conn_get_security(bt_conn *) { return security_level; }
static bt_conn *bt_conn_ref(bt_conn *c) { return c; }
static void bt_conn_unref(bt_conn *) {}
static uint16_t mtu = 247;
static uint16_t bt_gatt_get_mtu(bt_conn *) { return mtu; }
static int transport_error;
static unsigned sends, cancels, scans, adverts, disconnects, observed, unpairs, purges;
static unsigned settings_loads;
static int unpair_error;
static void k_msgq_purge(int *) { ++purges; }
#define BT_ID_DEFAULT 0
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 0x13
static bt_conn *bt_conn_lookup_addr_le(int, const bt_addr_le_t *) { ++cancels; return &fake_conn; }
static int bt_conn_disconnect(bt_conn *, uint8_t) { ++disconnects; return 0; }
static int bt_unpair(int, const void *) { ++unpairs; return unpair_error; }
static int bt_enable(void *) { return 0; }
static int settings_load_subtree(const char *name) {
    assert(strcmp(name,"bt")==0); ++settings_loads; return 0;
}
static void bt_id_get(bt_addr_le_t *addr,size_t *count) { *addr={}; *count=1; }
static void *last_ticket;
struct { int attrs[3]; } bridge_service;
struct bt_gatt_notify_params {
    int *attr; const void *data; uint16_t len;
    void (*func)(bt_conn *, void *); void *user_data;
};
static int bt_gatt_write_without_response_cb(bt_conn *, uint16_t, const void *,
        size_t, bool, void (*)(bt_conn *, void *), void *ticket) {
    ++sends; last_ticket=ticket; return transport_error;
}
static int bt_gatt_notify_cb(bt_conn *, bt_gatt_notify_params *p) {
    ++sends; last_ticket=p->user_data; return transport_error;
}
static bool mac_is_set(const uint8_t *p) { return p[0] != 0; }
static void repeater_bridge_peer_observed(const uint8_t *) { ++observed; }
static int bt_le_adv_stop() { return 0; }
static int bt_le_scan_stop() { return 0; }
static void start_scan() { ++scans; }
static void start_advertising() { ++adverts; }
static bool send_control(uint8_t, const uint8_t *);
static void maintain_tx_queue(int64_t);
static const uint8_t s_default_lmk[16]{};
#define BT_ADDR_LE_RANDOM 1
static constexpr int BRIDGE_BLE=0, FORWARD_PRIORITY_MAX=7;
static int s_transport=BRIDGE_BLE;
static const char *transport_name(int) { return "ble"; }
static const char *repeater_bridge_status() { return "connected"; }
static void repeater_bridge_get_addresses(char *local,size_t l,char *peer,size_t p) {
    snprintf(local,l,"76:79:3A:43:CA:48"); snprintf(peer,p,"9E:E8:62:0B:0D:D5");
}
'''

functions = [
    'static void schedule_retry(uint32_t delay_ms = RETRY_MS)',
    'static bool is_active_link(const struct bt_conn *conn)',
    'static bool link_is_established()',
    'static bool connection_is_peer(const struct bt_conn *conn)',
    'static uint32_t frame_hash(const uint8_t *raw, size_t len)',
    'static bool seen_or_remember(uint32_t hash)',
    'static struct bt_conn *link_conn_ref(bool require_ready, bool require_established,\n\t\tbool *is_central, uint16_t *peer_value_handle)',
    'static void purge_tx_queue()',
    'static void retry_in_flight_tx()',
    'static struct bt_conn *detach_link(const struct bt_conn *expected = nullptr)',
    'static void cancel_pending_connection()',
    'static void reset_link(const struct bt_conn *expected = nullptr)',
    'static bool clear_peer_bond()',
    'static void clear_discovery_state()',
    'static bool configure_peer()',
    'bool ble_bridge_start(RepeaterDataStore *store, mesh::Dispatcher *dispatcher)',
    'void ble_bridge_stop()',
    'bool ble_bridge_unpair(void)',
    'static void disconnected(struct bt_conn *conn, uint8_t reason)',
    'static void link_established()',
    'static void mark_link_ready(bool central)',
    'static uint32_t new_handshake_token()',
    'static bool handle_control(const BridgeFrame &frame)',
    'static void tx_complete(struct bt_conn *conn, void *user_data)',
    'static int send_frame(const BridgeFrame &frame, bool require_established, bool control)',
    'static bool send_control(uint8_t op, const uint8_t data[CONTROL_DATA_LEN])',
    'static bool accept_frame(struct bt_conn *conn, const void *data, uint16_t len)',
    'static uint8_t notification_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,\n\t\t\t\t   const void *data, uint16_t len)',
    'static void discovery_failed(struct bt_conn *conn, const char *stage, BridgeFault fault)',
    'static void subscribed(struct bt_conn *conn, uint8_t err,\n\t\tstruct bt_gatt_subscribe_params *params)',
    'static uint8_t ccc_discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,\n\t\t\t\t\tstruct bt_gatt_discover_params *params)',
    'static uint8_t characteristic_discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,\n\t\t\t\t\t\t   struct bt_gatt_discover_params *params)',
    'static uint8_t service_discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,\n\t\t\t\t\t  struct bt_gatt_discover_params *params)',
    'static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)',
    'static void connected(struct bt_conn *conn, uint8_t err)',
    'static void tx_work_handler(struct k_work *work)',
    'void ble_bridge_maintain(void)',
    'uint32_t ble_bridge_ms_until_next(void)',
    'static const char *fault_name(int fault)',
    'void ble_bridge_get_diagnostics(char *out, size_t out_len)',
]

tests = r'''
static void maintain_tx_queue(int64_t) { tx_work_handler(nullptr); }
static void fresh(bool connected=true) {
    purge_tx_queue(); s_started=1; s_prefs.peer_mac[0]=1;
    s_conn=connected ? &fake_conn : nullptr;
    s_is_central=true; s_link_ready=connected; s_link_established=connected;
    s_peer_value_handle=1; s_connect_deadline_ms=0; s_setup_deadline_ms=0;
    s_handshake_retry_ms=0; s_retry_at_ms=0; s_transport_refresh_ms=0;
    s_health_due_ms=0; s_health_deadline_ms=0; s_health_token=0;
    s_handshake_token=0; s_ping_token=0; s_drop_count=0;
    now_ms=1000; mtu=247; transport_error=0;
    sends=cancels=scans=adverts=disconnects=unpairs=purges=0;
    unpair_error=0;
    memcpy(fake_peer.a.val,s_prefs.peer_mac,6); fake_peer.type=s_prefs.peer_addr_type;
    rx_queued=0; security_level=2; memset(s_seen,0,sizeof(s_seen));
    discovers=subscribes=security_requests=0;
    discovery_error=subscribe_error=security_request_error=0;
    s_discovery_started=false; s_bond_recovery_attempted=0;
    s_last_fault=BRIDGE_FAULT_NONE; s_last_security_error=0; s_reconnect_count=0;
    clear_discovery_state();
}
static BridgeFrame data_frame() { BridgeFrame f{}; f.raw_len=234; return f; }
static BridgeFrame control(uint8_t op, uint32_t token) {
    BridgeFrame f{}; f.raw_len=CONTROL_LEN;
    memcpy(f.raw, CONTROL_MAGIC, sizeof(CONTROL_MAGIC));
    f.raw[sizeof(CONTROL_MAGIC)]=op;
    memcpy(f.raw+sizeof(CONTROL_MAGIC)+1, &token, sizeof(token)); return f;
}
static unsigned queued() {
    unsigned count=0; for (const auto &p:s_tx_queue) count+=p.used; return count;
}
int main() {
    mesh::Dispatcher dispatcher; s_dispatcher=&dispatcher;
    fresh(); unsigned wake_before=wakes;
    schedule_retry(); assert(wakes==wake_before+1);
    s_link_established=false;
    mark_link_ready(true); assert(wakes==wake_before+2);
    link_established(); assert(wakes==wake_before+3);
    fresh();
    // Past scan/retry deadlines must never spin main on a live ACL.
    s_retry_at_ms=100; s_transport_refresh_ms=100; s_health_due_ms=31000;
    assert(ble_bridge_ms_until_next()==30000);
    // Nor while a create operation is in progress.
    s_conn=nullptr; s_connect_deadline_ms=12000;
    assert(ble_bridge_ms_until_next()==11000);
    now_ms=12000; ble_bridge_maintain();
    assert(cancels==1 && scans==0 && s_retry_at_ms==13500);
    now_ms=13500; ble_bridge_maintain(); assert(scans==1);

    fresh(); s_link_established=false; s_setup_deadline_ms=100;
    s_handshake_retry_ms=100; s_retry_at_ms=100; s_transport_refresh_ms=100;
    link_established(); assert(ble_bridge_ms_until_next()==HEALTH_INTERVAL_MS);
    // A lost CCC must not expose expired health/handshake timers during setup.
    fresh(); s_link_established=false; s_link_ready=false;
    s_setup_deadline_ms=16000; s_health_due_ms=100; s_health_deadline_ms=100;
    s_handshake_retry_ms=100;
    assert(ble_bridge_ms_until_next()==15000);
    fresh(); s_health_token=42; s_health_due_ms=100; s_health_deadline_ms=7000;
    assert(ble_bridge_ms_until_next()==6000);

    // Hundreds of dropped completion callbacks must not exhaust a fixed pool.
    fresh();
    for (unsigned i=0;i<500;++i) {
        assert(send_frame(data_frame(),true,false)==0);
        tx_work_handler(nullptr); void *old=last_ticket;
        assert(s_tx_in_flight); retry_in_flight_tx();
        now_ms+=TX_DISCONNECTED_RETRY_MS;
        tx_work_handler(nullptr); void *current=last_ticket;
        assert(current!=old && s_tx_in_flight);
        tx_complete(&fake_conn,old); assert(s_tx_in_flight && queued()==1);
        tx_complete(&fake_conn,current); assert(!s_tx_in_flight && queued()==0);
    }
    // Off/on purges may reuse the very same slot.
    assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
    void *old=last_ticket; purge_tx_queue();
    assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
    tx_complete(&fake_conn,old); assert(s_tx_in_flight && queued()==1);
    tx_complete(&fake_conn,last_ticket); assert(queued()==0);

    fresh(); uint8_t payload[CONTROL_DATA_LEN]{};
    for (unsigned i=0;i<6;++i) assert(send_frame(data_frame(),true,false)==0);
    assert(send_frame(data_frame(),true,false)==-ENOMEM);
    assert(send_control(CONTROL_HELLO,payload)); assert(send_control(CONTROL_PONG,payload));
    assert(!send_control(CONTROL_PING,payload));
    tx_work_handler(nullptr); assert(s_tx_queue[6].in_flight);
    retry_in_flight_tx(); assert(queued()==6); // no old controls on reconnect

    fresh(); assert(send_frame(data_frame(),true,false)==0); mtu=23;
    for (unsigned i=0;i<10;++i) { tx_work_handler(nullptr); now_ms+=100; }
    assert(queued()==1 && sends==0 && s_tx_queue[0].attempts==0);
    mtu=247; tx_work_handler(nullptr); assert(s_tx_in_flight && sends==1);
    fresh(); assert(send_frame(data_frame(),true,false)==0); transport_error=-ENOMEM;
    for (unsigned i=0;i<10;++i) { tx_work_handler(nullptr); now_ms+=100; }
    assert(queued()==1 && s_tx_queue[0].attempts==0);
    now_ms+=TX_LIFETIME_MS; transport_error=0; unsigned before=sends;
    tx_work_handler(nullptr); assert(queued()==0 && sends==before && s_drop_count==1);

    fresh(); s_link_established=false; s_handshake_token=7;
    assert(handle_control(control(CONTROL_READY,7)) && s_link_established);
    purge_tx_queue(); assert(handle_control(control(CONTROL_READY,7)) && queued()==1);
    fresh(); s_is_central=false; s_link_established=false;
    assert(handle_control(control(CONTROL_HELLO,7)));
    assert(s_handshake_retry_ms==now_ms+RETRY_MS);
    purge_tx_queue(); now_ms+=RETRY_MS; ble_bridge_maintain();
    assert(queued()==1 && s_tx_queue[0].frame.raw[4]==CONTROL_READY);
    assert(handle_control(control(CONTROL_ACK,7)) && s_link_established);

    fresh(); s_health_due_ms=now_ms; s_ping_token=1;
    ble_bridge_maintain(); uint32_t health=uint32_t(s_health_token);
    assert((health & 0x80000000U) && health!=uint32_t(s_ping_token));
    assert(handle_control(control(CONTROL_PONG,health)));
    assert(s_health_token==0 && s_health_deadline_ms==0 && s_ping_token==1);
    fresh(); s_health_due_ms=now_ms; ble_bridge_maintain();
    now_ms+=HEALTH_TIMEOUT_MS; ble_bridge_maintain(); assert(disconnects==1 && !s_conn);
    fresh(); assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
    now_ms+=TX_IN_FLIGHT_TIMEOUT_MS; ble_bridge_maintain(); assert(disconnects==1 && !s_conn);
    fresh(); s_started=0;
    assert(ble_bridge_ms_until_next()==UINT32_MAX);
    assert(send_frame(data_frame(),true,false)==-ENOTCONN);
    // Remote reply has 161 bytes, including the optional three-byte prefix.
    fresh(); s_last_fault=BRIDGE_FAULT_SECURITY_REQUEST;
    s_last_disconnect_reason=0x3e; s_last_security_error=-12345;
    s_reconnect_count=INT32_MAX;
    struct { char reply[161]; uint8_t guard[32]; } buffer;
    memset(&buffer,0xa5,sizeof(buffer)); memcpy(buffer.reply,"01|",3);
    assert(status_reply("bridge",buffer.reply+3,sizeof(buffer.reply)-3));
    assert(strlen(buffer.reply)<sizeof(buffer.reply));
    for (auto byte:buffer.guard) assert(byte==0xa5);

    // Exercise the real lifecycle functions, not a reset/stop substitute.
    fresh(); s_bond_recovery_attempted=1;
    assert(send_control(CONTROL_HELLO,payload));
    assert(send_frame(data_frame(),true,false)==0);
    assert(ble_bridge_unpair());
    assert(!s_conn && !s_link_ready && !s_link_established);
    assert(unpairs==1 && s_bond_recovery_attempted==0 && queued()==1);
    assert(s_retry_at_ms==now_ms+RETRY_MS);
    fresh(); unpair_error=-EIO; assert(!ble_bridge_unpair() && !s_conn);
    fresh(); unpair_error=-ENOENT; assert(ble_bridge_unpair());
    fresh(); s_started=0; assert(!ble_bridge_unpair() && unpairs==0);
    fresh(); s_prefs.peer_mac[0]=0; assert(!ble_bridge_unpair() && unpairs==0);

    fresh(); bt_conn foreign;
    reset_link(&foreign); assert(s_conn==&fake_conn && disconnects==0);
    disconnected(&foreign,0x08); assert(s_conn==&fake_conn && !s_retry_at_ms);
    for (uint8_t reason: {0x08,0x16,0x22,0x3e}) {
        fresh(); assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
        disconnected(&fake_conn,reason);
        assert(!s_conn && !s_tx_in_flight && unpairs==0 && queued()==1);
        assert(s_retry_at_ms==now_ms+RETRY_MS);
    }

    fresh(); RepeaterDataStore store; stored_prefs.peer_mac[0]=1;
    assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
    void *stopped_ticket=last_ticket;
    ble_bridge_stop(); assert(!s_started && !s_conn && queued()==0 && purges==1);
    unsigned stop_calls=disconnects;
    ble_bridge_stop(); assert(disconnects==stop_calls && purges==1);
    tx_complete(&fake_conn,stopped_ticket); assert(!s_tx_in_flight);
    assert(ble_bridge_start(&store,&dispatcher));
    assert(s_started && s_is_central && settings_loads==1 && scans==1);
    ble_bridge_stop(); assert(ble_bridge_start(&store,&dispatcher));
    assert(settings_loads==1); // do not reload stale persistent bonds on off/on

    // Deterministic mixed operations: bounds and one-in-flight invariant.
    fresh(); uint32_t rng=0x34bd9871; std::vector<void *> tickets;
    for (unsigned i=0;i<100000;++i) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        switch (rng%7) {
        case 0: (void)send_frame(data_frame(),true,false); break;
        case 1: (void)send_control(CONTROL_PING,payload); break;
        case 2:
            transport_error=(rng&0x100)?-ENOMEM:0;
            tx_work_handler(nullptr);
            if (s_tx_in_flight) tickets.push_back(last_ticket);
            break;
        case 3:
            if (!tickets.empty()) tx_complete(&fake_conn,tickets[rng%tickets.size()]);
            break;
        case 4: retry_in_flight_tx(); break;
        case 5: purge_tx_queue(); break;
        case 6: now_ms+=rng%1000; break;
        }
        unsigned busy=0, data=0;
        for (const auto &p:s_tx_queue) { busy+=p.used&&p.in_flight; data+=p.used&&!p.control; }
        assert(busy<=1 && bool(busy)==s_tx_in_flight);
        assert(data<=TX_QUEUE_SLOTS-TX_CONTROL_RESERVE && queued()<=TX_QUEUE_SLOTS);
    }
    fresh(); s_tx_generation=UINT32_MAX;
    assert(send_frame(data_frame(),true,false)==0); tx_work_handler(nullptr);
    assert(s_tx_generation==1 && last_ticket);

    // Actual frame parser, keyed hash, security/peer/established gates.
    fresh(); BridgeFrame inbound=data_frame();
    inbound.magic=BRIDGE_MAGIC; inbound.version=BRIDGE_VERSION;
    inbound.hash=frame_hash(inbound.raw,inbound.raw_len);
    assert(accept_frame(&fake_conn,&inbound,sizeof(inbound)) && rx_queued==1);
    assert(accept_frame(&fake_conn,&inbound,sizeof(inbound)) && rx_queued==1); // duplicate
    fresh(); security_level=1;
    assert(!accept_frame(&fake_conn,&inbound,sizeof(inbound)) && !rx_queued);
    security_level=2; fake_peer.a.val[0]^=1;
    assert(!accept_frame(&fake_conn,&inbound,sizeof(inbound)) && !rx_queued);
    fresh(); s_link_established=false;
    assert(!accept_frame(&fake_conn,&inbound,sizeof(inbound)) && !rx_queued);
    fresh(); s_prefs.lmk[0]^=1;
    assert(!accept_frame(&fake_conn,&inbound,sizeof(inbound)) && !rx_queued);
    s_prefs.lmk[0]^=1;
    for (unsigned i=0;i<9;++i) {
        inbound.raw[0]=i; inbound.hash=frame_hash(inbound.raw,inbound.raw_len);
        assert(accept_frame(&fake_conn,&inbound,sizeof(inbound))==(i<8));
    }
    assert(rx_queued==8);
    fresh();
    for (unsigned length=0;length<=300;++length) {
        uint8_t wire[300]{}; memcpy(wire,&inbound,sizeof(inbound));
        if (length!=sizeof(inbound)) assert(!accept_frame(&fake_conn,wire,length));
    }
    for (unsigned i=0;i<10000;++i) {
        uint8_t wire[300];
        for (auto &b:wire) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; b=uint8_t(rng); }
        (void)accept_frame(&fake_conn,wire,rng%301);
    }
    // Connect callbacks after off and a foreign/duplicate connection.
    fresh(false); s_started=0; connected(&fake_conn,0);
    assert(disconnects==1 && !s_conn && security_requests==0);
    connected(&fake_conn,0x3e); assert(disconnects==1 && !s_retry_at_ms);
    fresh(false); connected(&fake_conn,0x3e);
    assert(s_retry_at_ms==now_ms+RETRY_MS && s_reconnect_count==1 && !s_conn);
    fresh(false); s_is_central=false; s_adv_running=true; fake_peer.a.val[0]^=1;
    connected(&fake_conn,0); assert(disconnects==1 && !s_conn && !s_adv_running);
    disconnected(&fake_conn,0x13); assert(s_retry_at_ms==now_ms+RETRY_MS && !unpairs);
    fresh(); connected(&foreign,0); assert(disconnects==1 && s_conn==&fake_conn);

    // Synchronous security request errors and a delayed L2 event.
    fresh(false); security_request_error=-EIO; connected(&fake_conn,0);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_SECURITY_REQUEST && !discovers);
    fresh(false); security_level=1; connected(&fake_conn,0);
    assert(s_conn && !s_link_ready && !discovers && security_requests==1);
    assert(s_setup_deadline_ms==now_ms+SETUP_TIMEOUT_MS);
    security_changed(&fake_conn,2,BT_SECURITY_ERR_SUCCESS);
    assert(discovers==1 && s_discovery_started && !s_link_ready);
    security_changed(&fake_conn,2,BT_SECURITY_ERR_SUCCESS); assert(discovers==1);

    // Full central callback chain: service -> value -> CCC -> confirmed subscription.
    bt_gatt_service_val service{30};
    bt_gatt_attr svc{&bridge_service_uuid.uuid,10,&service,0};
    bt_gatt_attr chr{&bridge_data_uuid.uuid,12,nullptr,13};
    bt_gatt_attr ccc{BT_UUID_GATT_CCC,14,nullptr,0};
    assert(service_discovery_cb(&fake_conn,&svc,&s_service_discover)==BT_GATT_ITER_STOP);
    assert(last_discovery==&s_characteristic_discover && last_discovery->start_handle==11);
    assert(last_discovery->end_handle==30);
    characteristic_discovery_cb(&fake_conn,&chr,&s_characteristic_discover);
    assert(s_peer_value_handle==13 && last_discovery==&s_ccc_discover);
    ccc_discovery_cb(&fake_conn,&ccc,&s_ccc_discover);
    assert(subscribes==1 && !s_link_ready); // enqueueing CCC is not ready
    assert(s_subscribe.flags[0] & (1<<BT_GATT_SUBSCRIBE_FLAG_VOLATILE));
    assert(s_subscribe.flags[0] & (1<<BT_GATT_SUBSCRIBE_FLAG_NO_RESUB));
    subscribed(&fake_conn,0,&s_subscribe);
    assert(s_link_ready && !s_link_established && s_handshake_retry_ms==now_ms);
    ble_bridge_maintain(); assert(queued()==1 && s_handshake_token);
    uint32_t hello=s_handshake_token;
    handle_control(control(CONTROL_READY,hello)); assert(s_link_established);
    notification_cb(&fake_conn,&s_subscribe,nullptr,0);
    assert(!s_conn && !s_link_established && s_last_fault==BRIDGE_FAULT_NOTIFY_LOST);
    assert(s_retry_at_ms==now_ms+RETRY_MS && !unpairs);

    // Failure at every discovery/subscription boundary remains recoverable.
    fresh(); service_discovery_cb(&fake_conn,nullptr,&s_service_discover);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_SERVICE);
    fresh(); service.end_handle=10; service_discovery_cb(&fake_conn,&svc,&s_service_discover);
    assert(disconnects==1 && !discovers); service.end_handle=30;
    fresh(); discovery_error=-ENOMEM; service_discovery_cb(&fake_conn,&svc,&s_service_discover);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_CHARACTERISTIC);
    fresh(); characteristic_discovery_cb(&fake_conn,nullptr,&s_characteristic_discover);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_CHARACTERISTIC);
    fresh(); s_service_end_handle=13;
    characteristic_discovery_cb(&fake_conn,&chr,&s_characteristic_discover);
    assert(disconnects==1 && !discovers);
    fresh(); ccc_discovery_cb(&fake_conn,nullptr,&s_ccc_discover);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_CCC);
    fresh(); s_peer_value_handle=0; ccc_discovery_cb(&fake_conn,&ccc,&s_ccc_discover);
    assert(disconnects==1 && !subscribes);
    fresh(); subscribe_error=-ENOMEM; ccc_discovery_cb(&fake_conn,&ccc,&s_ccc_discover);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_SUBSCRIBE);
    fresh(); subscribed(&fake_conn,0x05,&s_subscribe);
    assert(disconnects==1 && s_last_fault==BRIDGE_FAULT_SUBSCRIBE);
    fresh(); s_link_ready=false; subscribed(&foreign,0,&s_subscribe);
    assert(!s_link_ready && !disconnects);
    assert(notification_cb(&foreign,&s_subscribe,nullptr,0)==BT_GATT_ITER_STOP && s_conn);

    // SMP error policy: unpair only missing/rejected keys, at most once until success.
    for (int code=0;code<=BT_SECURITY_ERR_UNSPECIFIED;++code) {
        fresh(); auto error=static_cast<bt_security_err>(code);
        security_changed(&fake_conn,1,error);
        bool clear=code==BT_SECURITY_ERR_PIN_OR_KEY_MISSING || code==BT_SECURITY_ERR_KEY_REJECTED;
        assert(disconnects==1 && unpairs==unsigned(clear));
        security_changed(&fake_conn,1,error);
        assert(disconnects==2 && unpairs==unsigned(clear));
    }
    fresh(); security_changed(&foreign,1,BT_SECURITY_ERR_PIN_OR_KEY_MISSING);
    assert(!unpairs && !disconnects);
    fresh(); s_bond_recovery_attempted=1; link_established();
    assert(!s_bond_recovery_attempted);
    fresh(false); security_request_error=-EALREADY; connected(&fake_conn,0);
    assert(discovers==1 && !disconnects); // restored bonded encryption
    fresh(false); s_is_central=false; connected(&fake_conn,0);
    assert(s_link_ready && !s_link_established && !discovers);
    now_ms+=SETUP_TIMEOUT_MS; ble_bridge_maintain();
    assert(!s_conn && disconnects==1); // peer never sends HELLO
    now_ms+=RETRY_MS; ble_bridge_maintain(); assert(adverts==1);
    puts("BLE bridge: PASS (queue stress; RX fuzz; lifecycle; central GATT chain; discovery failures; SMP recovery policy; late callbacks; watchdogs; CLI bounds)");
}
'''

with tempfile.TemporaryDirectory(prefix="zephcore-bridge-test-") as temp:
    unit = Path(temp) / "test.cpp"
    binary = Path(temp) / "test"
    unit.write_text(shim + constants + records + globals_ +
                    "\n".join(definition(f) for f in functions) +
                    'static bool status_reply(const char *command, char *reply, size_t reply_len) {\n' +
                    status_branch + 'return false;\n}\n' + tests)
    subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall",
                    "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    *shlex.split(os.environ.get("BRIDGE_TEST_CXXFLAGS", "")),
                    str(unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
