# All-in-one ECU firmware: test cases (not run yet)

Firmware: the production build (`python build.py prod`), `1_XAIO_SW/code/main/ecu_main.cpp`. Setup: physical XIAO ESP32-S3 Sense with the camera,
L76K on D6/D7, MPU6050 on D4/D5, BLE antenna clipped on; phone with the ROVO app. Save the whole monitor output of
each run in a `runN_.../` folder here. Never save coordinates (the firmware does not print them).

| ID | Test | Steps | Expected | Result |
|---|---|---|---|---|
| TC-ECU-01 | Everything starts | Flash, reset, watch 1 min indoors | `IMU DETECTED`, `GNSS UART1 ready`, `CAM started`, `BLE advertising`; status line every 5 s; no reboot | not run |
| TC-ECU-02 | Long run, all parts | 10 min indoors, phone connected | no reboot or watchdog; `IMU ~100/s`, `CAM ok` rising, `fail` 0; `RAM min` and stacks stable | not run |
| TC-ECU-03 | IMU rate under load | Compare `IMU ../s` and `gaps` with the phone connected and Wi-Fi on vs. both off | stays ~100/s; gaps do not keep rising | not run |
| TC-ECU-04 | IMU pulled while running | Pull SDA 5 s, push back | `IMU LOST`, later `IMU BACK`; camera, GNSS and BLE keep going | not run |
| TC-ECU-05 | GNSS gone while running | Unplug the board, remove the L76K, plug in, then refit with the board unplugged | `GNSS bytes` stop rising, `fix=no`; other parts keep going; data back after refitting | not run |
| TC-ECU-06 | Phone reconnects | Turn phone Bluetooth off 30 s, then on | `BLE advertising`, then `subscribed` again; no reboot | not run |
| TC-ECU-07 | GNSS time and clock | Outdoors, open sky | `time=yes`, `ECU clock set from GNSS time`, `app=running`, `fix=yes` | not run |
| TC-ECU-08 | Real report to the phone | In a car (laptop logging), drive over a speed bump or pothole above 8 km/h | `ECU report signed #1`; the app shows the warning; `BLE acked` rises | not run |
| TC-ECU-09 | Same report to the backend | As TC-ECU-08 with Wi-Fi (phone hotspot) in `sdkconfig.local` | `WIFI stored` rises; the backend has the same event ID the phone got | not run |
| TC-ECU-10 | No report when stopped | Car stopped, shake the board | no `report signed` (speed below 8 km/h) | not run |
