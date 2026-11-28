// T-016: BLE reporting test, ESP-IDF NimBLE version of Mariam's NimBLE-Arduino code. Same UUIDs, frame format,
// log lines and transport rules, so the Flutter app and the test cases in T-016-ble-test-cases.md stay the same.
//
// Real: GATT server, advertising, fragmentation, retry and ACK state machine, and the report bytes: a signed
// schema-v2 report made by main/ble_main.cpp with the device's own key (the same format as POST /reports).
// Simulated: the hazard itself (a test pothole, no sensors in this build) and its time (see ble_main.cpp).
//
// Frame format:
//   byte 0-1 : transport ID, big endian, starts at 1, never 0
//   byte 2   : fragment index (zero based)
//   byte 3   : fragment count
//   byte 4.. : slice of the report bytes: min(MTU - 3, 244) - 4 bytes (16 at the default MTU of 23), fixed for a
//              report when it starts or restarts (firmware/ble/PROTOCOL.md)
//
// Transport rules:
//   - One report in flight at a time (stop-and-wait).
//   - Send the whole report again 2 s after its last fragment if no ACK came; 3 attempts, then stall and warn.
//     A late matching ACK is still accepted.
//   - A new connection or subscription sends the unacknowledged report again (same bytes, same transport ID)
//     from fragment 0 with fresh attempts. A new report is only made after an ACK.
//   - An ACK counts only after every fragment of the report was sent at least once.
//   - Transport IDs never wrap: after 65535 reports no new report is made until the next boot.
//   - A fragment whose notify fails is sent again on the next tick.
//
// Threading: NimBLE events run in the NimBLE host task and only set atomic flags. All report and fragment state
// belongs to the ble_app task.
//
// A wrong, early or badly sized ACK is refused with an ATT error (PROTOCOL.md), so the phone sees it failed.
//
// Two modes:
//   test (start_ble_test, T-016 firmware): make_report() makes a new signed test report each time; 10 s after an
//        ACK the next one is made. An empty result is a failure (BLE FAIL line).
//   link (start_ble_link, all-in-one ECU firmware): next_report() takes the next signed report from the ECU's
//        queue; empty means none waiting, which is normal, so it asks again every 200 ms without a FAIL line.
//        After an ACK the next queued report starts at once.
// In both modes the report bytes are taken first and the transport ID only after, so polling an empty queue never
// uses up transport IDs.
//
// Known gaps (bench test): no pairing, bonding or owner check (any phone can connect and ACK); the
// unacknowledged report is kept in RAM only (PROTOCOL.md asks for an NVS queue).
#include "ble_test.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {

void log_line(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// 7b10000x-6a34-4c82-9b51-2f048e9a6300, bytes in little-endian order as NimBLE stores them.
#define ICVSP_UUID(x) BLE_UUID128_INIT(0x00, 0x63, 0x9a, 0x8e, 0x04, 0x2f, 0x51, 0x9b, 0x82, 0x4c, 0x34, 0x6a, x, 0x00, 0x10, 0x7b)
const ble_uuid128_t kServiceUuid = ICVSP_UUID(0x01);
const ble_uuid128_t kReportUuid = ICVSP_UUID(0x02);
const ble_uuid128_t kAckUuid = ICVSP_UUID(0x03);

constexpr size_t kMinChunkBytes = 16;   // at the default MTU of 23
constexpr size_t kMaxChunkBytes = 240;  // min(MTU - 3, 244) - 4 at MTU 247 and more
constexpr uint32_t kRetryTimeoutMs = CONFIG_ICVSP_BLE_RETRY_MS;
constexpr uint8_t kMaxAttempts = CONFIG_ICVSP_BLE_MAX_ATTEMPTS;
constexpr uint32_t kSendTickMs = 30;
constexpr uint32_t kReportGapMs = CONFIG_ICVSP_BLE_REPORT_GAP_MS;

ReportMaker g_make_report = nullptr;
bool g_link_mode = false;
constexpr uint32_t kLinkPollMs = 200;  // link mode: how often an empty report queue is asked again
std::atomic<uint32_t> g_acked_count{0};  // copy of g_acked_total for ble_link_status() (other tasks)

uint16_t g_report_handle = 0;
uint8_t g_own_addr_type = 0;

// Set by NimBLE events, read by the ble_app task.
std::atomic<bool> g_connected{false};
std::atomic<uint16_t> g_conn_handle{BLE_HS_CONN_HANDLE_NONE};
std::atomic<bool> g_subscribed{false};
std::atomic<bool> g_restart_transport{false};  // new subscription or disconnect: start the report again
std::atomic<uint16_t> g_in_flight_id{0};        // copy of g_transport_id for the ACK handler (0 = none)
std::atomic<uint16_t> g_acked_id{0};            // matching ACK received, handled by the task
std::atomic<bool> g_sent_complete{false};       // every fragment of the in-flight report sent at least once

// Owned by the ble_app task only.
std::string g_report_bytes;
uint16_t g_transport_id = 0;
uint8_t g_fragment_count = 0;
size_t g_chunk_bytes = kMinChunkBytes;
uint32_t g_next_report_ms = 0;  // after an ACK, the next report starts at this time
uint8_t g_next_fragment = 0;
uint8_t g_attempt = 0;
uint32_t g_last_send_ms = 0;
bool g_stalled = false;
uint32_t g_acked_total = 0;

// 0 = exhausted: transport IDs must not wrap within a boot (a stale ACK could match a later report).
uint16_t g_last_transport_id = 0;
bool transport_ids_exhausted() { return g_last_transport_id == 0xFFFF; }
uint16_t next_transport_id()
{
    if (transport_ids_exhausted()) return 0;
    return ++g_last_transport_id;
}

void reset_transport_state()
{
    g_transport_id = 0;
    g_in_flight_id = 0;
    g_sent_complete = false;
    g_fragment_count = 0;
    g_next_fragment = 0;
    g_attempt = 0;
    g_stalled = false;
}

// Report bytes per fragment for the current connection: min(MTU - 3, 244) - 4 (PROTOCOL.md).
size_t chunk_bytes_now()
{
    const uint16_t mtu = ble_att_mtu(g_conn_handle);
    size_t n = mtu > 7 ? static_cast<size_t>(mtu) - 3 : 0;
    if (n > 244) n = 244;
    n = n > 4 ? n - 4 : 0;
    if (n < kMinChunkBytes) n = kMinChunkBytes;
    if (n > kMaxChunkBytes) n = kMaxChunkBytes;
    return n;
}

// Fixes the fragment size for the report from the current MTU. False if the report needs more than 255 fragments.
bool split_report()
{
    g_chunk_bytes = chunk_bytes_now();
    const size_t count = (g_report_bytes.size() + g_chunk_bytes - 1) / g_chunk_bytes;
    if (count == 0 || count > 255) return false;
    g_fragment_count = static_cast<uint8_t>(count);
    return true;
}

void start_new_report()
{
    if (transport_ids_exhausted()) {  // checked before taking a report, so no report is taken and then lost
        static bool warned = false;
        if (!warned) log_line("BLE WARN transport IDs exhausted (65535 reports this boot): no new reports until reset\n");
        warned = true;
        reset_transport_state();
        g_report_bytes.clear();
        return;
    }
    g_report_bytes = g_make_report ? g_make_report() : std::string();
    if (g_link_mode && g_report_bytes.empty()) {  // nothing queued: ask again soon, quietly
        reset_transport_state();
        g_next_report_ms = millis() + kLinkPollMs;
        return;
    }
    if (g_report_bytes.empty() || !split_report()) {
        log_line("BLE FAIL could not make a signed report (%u bytes), none sent\n",
                 static_cast<unsigned>(g_report_bytes.size()));
        reset_transport_state();
        g_report_bytes.clear();
        g_next_report_ms = millis() + (g_link_mode ? kLinkPollMs : kReportGapMs);
        return;
    }
    const uint16_t id = next_transport_id();  // never 0 here: exhaustion was checked above
    g_transport_id = id;
    g_next_fragment = 0;
    g_attempt = 1;
    g_last_send_ms = 0;
    g_stalled = false;
    g_sent_complete = false;
    g_in_flight_id = g_transport_id;
    log_line("BLE new report transport_id=%u fragments=%u bytes=%u chunk=%u\n", g_transport_id, g_fragment_count,
             static_cast<unsigned>(g_report_bytes.size()), static_cast<unsigned>(g_chunk_bytes));
}

// After a reconnect or resubscribe: the same unacknowledged report again from fragment 0, attempts reset.
void restart_head()
{
    g_next_fragment = 0;
    g_attempt = 1;
    g_last_send_ms = 0;
    g_stalled = false;
    g_sent_complete = false;  // the phone cleared its partial copy, so it must see every fragment again
    split_report();           // the new link may have another MTU; same bytes, same transport ID
    log_line("BLE restarting unacknowledged report transport_id=%u from fragment 1 (fresh attempts, %u fragments)\n",
             g_transport_id, g_fragment_count);
}

// True if the notification was queued; on false the same fragment is sent again on the next tick.
bool send_fragment(uint8_t index)
{
    const size_t offset = static_cast<size_t>(index) * g_chunk_bytes;
    const size_t remaining = g_report_bytes.size() - offset;
    const size_t len = remaining < g_chunk_bytes ? remaining : g_chunk_bytes;

    uint8_t frame[4 + kMaxChunkBytes];
    frame[0] = static_cast<uint8_t>(g_transport_id >> 8);
    frame[1] = static_cast<uint8_t>(g_transport_id & 0xFF);
    frame[2] = index;
    frame[3] = g_fragment_count;
    memcpy(frame + 4, g_report_bytes.data() + offset, len);

    os_mbuf* om = ble_hs_mbuf_from_flat(frame, static_cast<uint16_t>(4 + len));
    const int rc = om == nullptr ? BLE_HS_ENOMEM : ble_gatts_notify_custom(g_conn_handle, g_report_handle, om);
    if (rc != 0) {
        log_line("BLE WARN notify failed transport_id=%u fragment=%u/%u rc=%d, trying again\n", g_transport_id,
                 index + 1, g_fragment_count, rc);
        return false;
    }
    g_last_send_ms = millis();
    log_line("BLE notify transport_id=%u fragment=%u/%u attempt=%u\n", g_transport_id, index + 1, g_fragment_count,
             g_attempt);
    return true;
}

int report_access(uint16_t, uint16_t, ble_gatt_access_ctxt*, void*)
{
    return BLE_ATT_ERR_READ_NOT_PERMITTED;  // notify only; never called for reads (no READ flag)
}

int ack_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != 2) {
        log_line("BLE WARN ACK write with bad length=%u (want 2), refused\n", len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t value[2];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, value, sizeof value, &copied) != 0 || copied != 2) return BLE_ATT_ERR_UNLIKELY;
    const uint16_t acked_id = static_cast<uint16_t>((value[0] << 8) | value[1]);
    const uint16_t in_flight = g_in_flight_id;
    if (in_flight == 0 || acked_id != in_flight) {
        log_line("BLE WARN ACK id=%u does not match in-flight transport_id=%u, refused\n", acked_id, in_flight);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if (!g_sent_complete) {
        log_line("BLE WARN early ACK id=%u before all fragments were sent, refused\n", acked_id);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    g_acked_id = acked_id;  // the task logs it and starts the next report
    return 0;
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kReportUuid.u,
        .access_cb = report_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_report_handle,
    },
    {
        .uuid = &kAckUuid.u,
        .access_cb = ack_access,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {},  // end of list
};

const ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .characteristics = kCharacteristics,
    },
    {},  // end of list
};

void start_advertising();

int gap_event(ble_gap_event* event, void*)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ble_gap_conn_desc desc;
                g_conn_handle = event->connect.conn_handle;
                g_connected = true;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    const uint8_t* a = desc.peer_id_addr.val;
                    log_line("BLE central connected address=%02x:%02x:%02x:%02x:%02x:%02x\n", a[5], a[4], a[3], a[2],
                             a[1], a[0]);
                } else {
                    log_line("BLE central connected\n");
                }
            } else {
                log_line("BLE WARN connection failed status=%d, advertising again\n", event->connect.status);
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            g_connected = false;
            g_subscribed = false;
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            g_restart_transport = true;
            log_line("BLE central disconnected reason=0x%x, advertising again\n", event->disconnect.reason);
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == g_report_handle) {
                const bool now_subscribed = event->subscribe.cur_notify != 0;
                if (now_subscribed && !g_subscribed) {
                    log_line("BLE central subscribed, starting report transport\n");
                } else if (!now_subscribed && g_subscribed) {
                    log_line("BLE central unsubscribed\n");
                }
                g_subscribed = now_subscribed;
                g_restart_transport = true;
            }
            return 0;
        case BLE_GAP_EVENT_MTU:
            log_line("BLE MTU %u (used from the next report or restart)\n", event->mtu.value);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            return 0;
        default:
            return 0;
    }
}

// Legacy (BLE 4) advertising, seen by every phone scanner. Flags and the 128-bit service UUID go in the
// advertising packet (what the phone filters on); the name goes in the scan response, because all three together
// are over the 31-byte limit. The name ICVSP-001 is not unique and not a secure identity.
void start_advertising()
{
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &kServiceUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) {
        ble_hs_adv_fields scan{};
        const char* name = ble_svc_gap_device_name();
        scan.name = reinterpret_cast<const uint8_t*>(name);
        scan.name_len = static_cast<uint8_t>(strlen(name));
        scan.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&scan);
    }
    if (rc == 0) {
        ble_gap_adv_params params{};
        params.conn_mode = BLE_GAP_CONN_MODE_UND;
        params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
    }
    if (rc != 0 && rc != BLE_HS_EALREADY) log_line("BLE WARN advertising did not start rc=%d\n", rc);
}

void on_sync()
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &g_own_addr_type) != 0) {
        log_line("BLE FAIL no usable Bluetooth address\n");
        return;
    }
    log_line("[step 2/3] BLE: advertise as %s with the ICVSP service UUID\n", CONFIG_ICVSP_BLE_DEVICE_NAME);
    start_advertising();
    uint8_t a[6] = {};
    ble_hs_id_copy_addr(g_own_addr_type, a, nullptr);
    log_line("BLE advertising as %s, address %02x:%02x:%02x:%02x:%02x:%02x\n", CONFIG_ICVSP_BLE_DEVICE_NAME, a[5],
             a[4], a[3], a[2], a[1], a[0]);
    log_line("[step 3/3] BLE: waiting for a phone to connect and subscribe\n");
}

void on_reset(int reason) { log_line("BLE WARN Bluetooth host reset, reason=%d\n", reason); }

void host_task(void*)
{
    nimble_port_run();  // returns only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

void ble_task(void*)
{
    uint32_t last_tick = 0;
    for (;;) {
        const uint32_t now = millis();
        if (now - last_tick >= kSendTickMs) {
            last_tick = now;

            if (g_restart_transport.exchange(false)) {
                g_acked_id = 0;  // an ACK from the previous session no longer counts
                if (g_connected && g_subscribed) {
                    if (g_transport_id != 0) {
                        restart_head();  // keep the unacknowledged report
                    } else {
                        start_new_report();
                    }
                }
            }
            const uint16_t acked = g_acked_id.exchange(0);
            if (acked != 0 && acked == g_transport_id && g_sent_complete) {
                g_acked_total++;
                g_acked_count = g_acked_total;
                log_line("BLE ACK accepted transport_id=%u (%lu acknowledged so far)\n", acked,
                         static_cast<unsigned long>(g_acked_total));
                reset_transport_state();
                // test mode: the next test pothole after a pause; link mode: the next queued report at once
                g_next_report_ms = millis() + (g_link_mode ? 1 : kReportGapMs);
            }

            if (g_connected && g_subscribed && g_transport_id == 0 && g_next_report_ms != 0 &&
                static_cast<int32_t>(now - g_next_report_ms) >= 0) {
                g_next_report_ms = 0;
                start_new_report();
            }

            if (g_connected && g_subscribed && g_transport_id != 0 && !g_stalled) {
                if (g_next_fragment < g_fragment_count) {
                    if (send_fragment(g_next_fragment)) {
                        g_next_fragment++;
                        if (g_next_fragment == g_fragment_count) g_sent_complete = true;
                    }
                } else if (now - g_last_send_ms >= kRetryTimeoutMs) {
                    if (g_attempt >= kMaxAttempts) {
                        g_stalled = true;
                        log_line("BLE WARN transport_id=%u stalled after %u attempts (a late ACK is still accepted)\n",
                                 g_transport_id, g_attempt);
                    } else {
                        g_attempt++;
                        g_next_fragment = 0;
                        log_line("BLE no ACK after %lu ms, sending transport_id=%u again (attempt %u/%u)\n",
                                 static_cast<unsigned long>(kRetryTimeoutMs), g_transport_id, g_attempt, kMaxAttempts);
                    }
                }
            }
        }
        vTaskDelay(1);  // 10 ms at the 100 Hz tick
    }
}

}  // namespace

namespace {

void start_ble(ReportMaker source, bool link_mode)
{
    g_make_report = source;
    g_link_mode = link_mode;
    log_line("BLE starting BLE GATT server (ESP-IDF NimBLE, %s, no pairing)\n",
             link_mode ? "the ECU's signed reports" : "signed test reports");
    log_line("[step 1/3] BLE: start the Bluetooth stack and the GATT service\n");

    esp_err_t err = nvs_flash_init();  // the Bluetooth controller keeps its calibration data in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        log_line("BLE FAIL NVS needs erasing (%s); not erased automatically\n", esp_err_to_name(err));
        return;
    }
    if (err == ESP_OK) err = nimble_port_init();
    if (err != ESP_OK) {
        log_line("BLE FAIL Bluetooth start: %s\n", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(kServices);
    if (rc == 0) rc = ble_gatts_add_svcs(kServices);
    if (rc == 0) rc = ble_svc_gap_device_name_set(CONFIG_ICVSP_BLE_DEVICE_NAME);
    if (rc != 0) {
        log_line("BLE FAIL GATT service setup rc=%d\n", rc);
        return;
    }

    xTaskCreate(ble_task, "ble_app", 6144, nullptr, 5, nullptr);
    nimble_port_freertos_init(host_task);  // on_sync runs once the stack is ready, then advertising starts
}

}  // namespace

void start_ble_test(ReportMaker make_report) { start_ble(make_report, false); }

void start_ble_link(ReportMaker next_report) { start_ble(next_report, true); }

BleLinkStatus ble_link_status()
{
    BleLinkStatus st;
    st.connected = g_connected;
    st.subscribed = g_subscribed;
    st.in_flight_id = g_in_flight_id;
    st.acked = g_acked_count;
    return st;
}
