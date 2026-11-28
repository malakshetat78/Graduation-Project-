#pragma once
// T-016 BLE bring-up test (original NimBLE-Arduino version by Mariam), on the ESP-IDF NimBLE host.
// GATT service matching the Flutter app (ble_service.dart, ble_report_assembler.dart):
//   Service : 7b100001-6a34-4c82-9b51-2f048e9a6300
//   Report  : 7b100002-6a34-4c82-9b51-2f048e9a6300 (Notify)
//   Ack     : 7b100003-6a34-4c82-9b51-2f048e9a6300 (Write with response)
// Advertises, and once a phone subscribes sends signed reports in fragments, with ACK, retry and stall handling.
// make_report returns one complete signed report line (empty on failure); it is called on the BLE task each time
// a new report is needed. Starts its own tasks and returns.
#include <cstdint>
#include <string>
using ReportMaker = std::string (*)();

// T-016 test firmware: make_report makes a new signed test report each time (empty = failure).
void start_ble_test(ReportMaker make_report);

// All-in-one ECU firmware: next_report returns the next queued signed report, or an empty string when none is
// waiting (normal; asked again every 200 ms). Same GATT service, frames, ACK and retry rules as the test.
void start_ble_link(ReportMaker next_report);

struct BleLinkStatus {
    bool connected = false;
    bool subscribed = false;
    uint16_t in_flight_id = 0;  // 0 = no report being sent
    uint32_t acked = 0;         // reports the phone acknowledged since boot
};
BleLinkStatus ble_link_status();  // safe to call from any task
