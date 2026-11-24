// ICVSP BLE reporting app (NimBLE-Arduino 2.x). Original by Mariam, adapted for the ICVSP_ECU sketch.
//
// Real: BLE GATT server setup/advertising, fragmentation/retry/ACK transport state machine, matching the UUIDs
// and frame format the Flutter app (ble_service.dart, ble_report_assembler.dart) expects.
// Simulated: the "report" bytes are a placeholder JSON string, not a real Ed25519-signed report and not the
// agreed hazard event schema. Wiring this to the real signed report producer is a follow-up step.
//
// Frame format (PROTOCOL.md):
//   byte 0-1 : transport ID, big endian, starts at 1, never 0
//   byte 2   : fragment index (zero based)
//   byte 3   : fragment count
//   byte 4.. : opaque slice of the report bytes
//
// Transport behaviour:
//   - One report in flight at a time (stop-and-wait).
//   - Retry the whole message 2 s after the last accepted fragment if no ACK.
//   - Up to 3 attempts per subscription; then stall and log a warning. A late matching ACK is still accepted.
//   - A new connection/subscription restarts the unacknowledged report (same bytes, same transport ID) at
//     fragment 0 with fresh attempts. A new report is only made after its ACK.
//   - An ACK is only accepted after every fragment of the report was sent at least once.
//   - Transport IDs never wrap: after 65535 reports no new report is started until the next boot.
//   - A fragment whose notify fails is sent again on the next tick (the cursor does not advance).
//
// Threading: NimBLE callbacks run in the Bluetooth host task. They only set flags; all report and fragment state
// is owned by the ble_app task, so a callback can never rebuild the report while a fragment is being copied.
//
// Known gaps against PROTOCOL.md (bench): no pairing/bonding/owner check (any phone can connect and ACK);
// wrong/early ACKs are logged and ignored but the phone still gets a successful write response (the protocol
// asks for an ATT error, which NimBLE-Arduino's onWrite callback cannot return); chunk size is fixed at 16 bytes
// (MTU 23) instead of following the negotiated MTU; the unacknowledged report is kept in RAM only (PROTOCOL.md
// asks for an eight-report NVS queue that survives a restart).

#include "ble_app.h"

#include "../../config.h"

// Compiled only when config.h selects the BLE program. The Arduino IDE builds every .cpp in src/, so without
// this guard the IMU, camera, smoke and report programs would also need the NimBLE-Arduino library installed.
#if ICVSP_APP == ICVSP_APP_BLE

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <atomic>

#include "log.h"

namespace {

constexpr char SERVICE_UUID[] = "7b100001-6a34-4c82-9b51-2f048e9a6300";
constexpr char REPORT_CHAR_UUID[] = "7b100002-6a34-4c82-9b51-2f048e9a6300";
constexpr char ACK_CHAR_UUID[] = "7b100003-6a34-4c82-9b51-2f048e9a6300";

constexpr uint8_t CHUNK_PAYLOAD_BYTES = 16;  // safe at MTU 23
constexpr uint32_t RETRY_TIMEOUT_MS = 2000;
constexpr uint8_t MAX_ATTEMPTS = 3;
constexpr uint32_t SEND_TICK_MS = 30;

NimBLEServer* g_server = nullptr;
NimBLECharacteristic* g_reportChar = nullptr;

// Set by the NimBLE callbacks, read by the ble_app task.
std::atomic<bool> g_connected{false};
std::atomic<bool> g_subscribed{false};
std::atomic<bool> g_restart_transport{false};  // new subscription or disconnect: drop the in-flight message
std::atomic<uint16_t> g_in_flight_id{0};        // copy of g_transportId for the ACK callback (0 = none)
std::atomic<uint16_t> g_acked_id{0};            // matching ACK received, handled by the task
std::atomic<bool> g_sent_complete{false};       // every fragment of the in-flight report sent at least once
std::atomic<bool> g_status_request{false};      // ble_status() asks the task to print its state

// Owned by the ble_app task only.
String g_reportBytes;
uint16_t g_transportId = 0;
uint8_t g_fragmentCount = 0;
uint8_t g_nextFragment = 0;
uint8_t g_attempt = 0;
uint32_t g_lastSendMs = 0;
bool g_stalled = false;
uint32_t g_acked_total = 0;

// 0 = exhausted: PROTOCOL.md forbids wrapping within a boot (a stale ACK could match a later report).
uint16_t next_transport_id() {
  static uint16_t counter = 0;
  if (counter == 0xFFFF) return 0;
  return ++counter;
}

void reset_transport_state();

// Placeholder "signed report". Replace with the real signed bytes from the report pipeline.
String make_simulated_report() {
  String body = "{\"sim\":true,\"ts\":";
  body += String((uint32_t)millis());
  body += ",\"lat\":30.0444,\"lon\":31.2357}";
  return body;
}

void start_new_report() {
  const uint16_t id = next_transport_id();
  if (id == 0) {
    static bool warned = false;
    if (!warned) log_line("BLE WARN transport IDs exhausted (65535 reports this boot): no new reports until reset\n");
    warned = true;
    reset_transport_state();
    g_reportBytes = "";
    return;
  }
  g_reportBytes = make_simulated_report();
  g_transportId = id;
  g_fragmentCount = (g_reportBytes.length() + CHUNK_PAYLOAD_BYTES - 1) / CHUNK_PAYLOAD_BYTES;
  if (g_fragmentCount == 0) g_fragmentCount = 1;
  g_nextFragment = 0;
  g_attempt = 1;
  g_lastSendMs = 0;
  g_stalled = false;
  g_sent_complete = false;
  g_in_flight_id = g_transportId;
  log_line("BLE new report transport_id=%u fragments=%u bytes=%u\n", g_transportId, g_fragmentCount,
           (unsigned)g_reportBytes.length());
}

void reset_transport_state() {
  g_transportId = 0;
  g_in_flight_id = 0;
  g_sent_complete = false;
  g_fragmentCount = 0;
  g_nextFragment = 0;
  g_attempt = 0;
  g_stalled = false;
}

// After a reconnect/resubscribe: send the same unacknowledged report again from fragment 0, attempts reset.
void restart_head() {
  g_nextFragment = 0;
  g_attempt = 1;
  g_lastSendMs = 0;
  g_stalled = false;
  g_sent_complete = false;  // the phone cleared its partial copy, so it must see every fragment again
  log_line("BLE restarting unacknowledged report transport_id=%u from fragment 1 (fresh attempts)\n", g_transportId);
}

// Returns true if the notification was queued; on false the caller sends the same fragment again next tick.
bool send_fragment(uint8_t index) {
  const uint16_t offset = (uint16_t)index * CHUNK_PAYLOAD_BYTES;
  const uint16_t remaining = g_reportBytes.length() - offset;
  const uint8_t len = remaining < CHUNK_PAYLOAD_BYTES ? remaining : CHUNK_PAYLOAD_BYTES;

  uint8_t frame[4 + CHUNK_PAYLOAD_BYTES];
  frame[0] = (g_transportId >> 8) & 0xFF;
  frame[1] = g_transportId & 0xFF;
  frame[2] = index;
  frame[3] = g_fragmentCount;
  memcpy(frame + 4, g_reportBytes.c_str() + offset, len);

  if (!g_reportChar->notify(frame, 4 + len)) {
    log_line("BLE WARN notify failed transport_id=%u fragment=%u/%u, trying again\n", g_transportId, index + 1,
             g_fragmentCount);
    return false;
  }
  g_lastSendMs = millis();
  log_line("BLE notify transport_id=%u fragment=%u/%u attempt=%u\n", g_transportId, index + 1, g_fragmentCount,
           g_attempt);
  return true;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    (void)server;
    g_connected = true;
    log_line("BLE central connected address=%s\n", connInfo.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    g_connected = false;
    g_subscribed = false;
    g_restart_transport = true;
    log_line("BLE central disconnected reason=0x%x, advertising again\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

class ReportCharCallbacks : public NimBLECharacteristicCallbacks {
  // Fires when the phone writes the Report characteristic's CCCD (subscribe/unsubscribe).
  // NimBLE manages the CCCD descriptor itself; no manual descriptor needed.
  void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)characteristic;
    (void)connInfo;
    const bool nowSubscribed = subValue != 0;
    if (nowSubscribed && !g_subscribed) {
      log_line("BLE central subscribed, starting report transport\n");
    } else if (!nowSubscribed && g_subscribed) {
      log_line("BLE central unsubscribed\n");
    }
    g_subscribed = nowSubscribed;
    g_restart_transport = true;
  }
};

class AckCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    const std::string value = characteristic->getValue();
    if (value.length() != 2) {
      log_line("BLE WARN ACK write with bad length=%u (want 2), ignored\n", (unsigned)value.length());
      return;
    }
    const uint16_t ackedId = ((uint8_t)value[0] << 8) | (uint8_t)value[1];
    const uint16_t inFlight = g_in_flight_id;
    if (inFlight == 0 || ackedId != inFlight) {
      log_line("BLE WARN ACK id=%u does not match in-flight transport_id=%u, ignored\n", ackedId, inFlight);
      return;
    }
    if (!g_sent_complete) {
      log_line("BLE WARN early ACK id=%u before all fragments were sent, ignored\n", ackedId);
      return;
    }
    g_acked_id = ackedId;  // the task logs it and starts the next report
  }
};

void ble_task(void*) {
  uint32_t lastTick = 0;
  for (;;) {
    const uint32_t now = millis();
    if (now - lastTick >= SEND_TICK_MS) {
      lastTick = now;

      if (g_restart_transport.exchange(false)) {
        g_acked_id = 0;  // an ACK from the previous session no longer counts
        if (g_connected && g_subscribed) {
          if (g_transportId != 0) restart_head();  // keep the unacknowledged report
          else start_new_report();
        }
      }
      const uint16_t acked = g_acked_id.exchange(0);
      if (acked != 0 && acked == g_transportId && g_sent_complete) {
        g_acked_total++;
        log_line("BLE ACK accepted transport_id=%u (%lu acknowledged so far)\n", acked, (unsigned long)g_acked_total);
        reset_transport_state();
        if (g_connected && g_subscribed) start_new_report();  // demo loop: queue the next simulated report
      }
      if (g_status_request.exchange(false)) {
        log_line("BLE status connected=%d subscribed=%d transport_id=%u fragment=%u/%u attempt=%u stalled=%d "
                 "acked=%lu\n", (int)g_connected, (int)g_subscribed, g_transportId, g_nextFragment, g_fragmentCount,
                 g_attempt, (int)g_stalled, (unsigned long)g_acked_total);
      }

      if (g_connected && g_subscribed && g_transportId != 0 && !g_stalled) {
        if (g_nextFragment < g_fragmentCount) {
          if (send_fragment(g_nextFragment)) {
            g_nextFragment++;
            if (g_nextFragment == g_fragmentCount) g_sent_complete = true;
          }
        } else if (now - g_lastSendMs >= RETRY_TIMEOUT_MS) {
          if (g_attempt >= MAX_ATTEMPTS) {
            g_stalled = true;
            log_line("BLE WARN transport_id=%u stalled after %u attempts (a late ACK is still accepted)\n",
                     g_transportId, g_attempt);
          } else {
            g_attempt++;
            g_nextFragment = 0;
            log_line("BLE no ACK after %lu ms, sending transport_id=%u again (attempt %u/%u)\n",
                     (unsigned long)RETRY_TIMEOUT_MS, g_transportId, g_attempt, MAX_ATTEMPTS);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

void start_ble_app() {
  log_line("BLE starting BLE GATT server (simulated report payload, no pairing)\n");
  step(1, 3, "BLE: start the Bluetooth stack and the GATT service");

  NimBLEDevice::init("ICVSP-001");
  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = g_server->createService(SERVICE_UUID);
  g_reportChar = service->createCharacteristic(REPORT_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);
  g_reportChar->setCallbacks(new ReportCharCallbacks());
  NimBLECharacteristic* ackChar = service->createCharacteristic(ACK_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
  ackChar->setCallbacks(new AckCallbacks());
  g_server->start();

  // Legacy (BLE 4) advertising: every phone scanner sees it. Scan by the service UUID: the name ICVSP-001 is
  // not unique and not a secure identity.
  step(2, 3, "BLE: advertise as ICVSP-001 with the ICVSP service UUID");
  // Flags (3) + 128-bit UUID (18) + name (11) is 32 bytes, over the 31-byte advertising limit. Scan response
  // must be enabled BEFORE setName so NimBLE puts the name there and keeps the service UUID (what the phone
  // filters on) in the main advertising packet.
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setName("ICVSP-001");
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();
  log_line("BLE advertising as ICVSP-001, address %s\n", NimBLEDevice::getAddress().toString().c_str());

  step(3, 3, "BLE: waiting for a phone to connect and subscribe");
  xTaskCreate(ble_task, "ble_app", 6144, nullptr, 5, nullptr);
}

void ble_status() {
  g_status_request = true;  // the ble_app task prints it, so the values are never read while being changed
}

#endif  // ICVSP_APP == ICVSP_APP_BLE
