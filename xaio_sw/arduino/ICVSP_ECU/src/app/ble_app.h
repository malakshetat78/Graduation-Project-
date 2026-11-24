#pragma once
// ICVSP BLE reporting app (NimBLE-Arduino 2.x). Original by Mariam, adapted for the ICVSP_ECU sketch.
// GATT service matching the Flutter ble_service.dart / PROTOCOL.md bench contract:
//   Service   : 7b100001-6a34-4c82-9b51-2f048e9a6300
//   Report    : 7b100002-6a34-4c82-9b51-2f048e9a6300 (Notify)
//   Ack       : 7b100003-6a34-4c82-9b51-2f048e9a6300 (Write with response)
// Starts the BLE server and advertising, then sends simulated report-sized messages over the Report
// characteristic once a phone subscribes. Runs in its own FreeRTOS task, so start_ble_app() returns.
void start_ble_app();
void ble_status();  // debug: one line with the connection and transport state