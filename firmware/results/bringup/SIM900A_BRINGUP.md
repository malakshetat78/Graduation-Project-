# SIM900A (2G) bring-up plan: fallback uplink for the ECU

Status: **plan only, nothing run.** Written so far: the AT bridge firmware (below); no PPP/modem network code yet. The ECU's main uplink stays Wi-Fi through a phone
hotspot (`BRINGUP_CHECKLIST.md` section 4b, `TEST_CASES.md` TC-18..TC-24). This plan is the "just in case" path the
mentor offered: a SIMCom SIM900A on a blue "SIM900A MINI" breakout (SIM slot, SMA antenna), giving the ECU its own
mobile data with no phone.

What stays the same with the SIM path: detection, signing, the delivery queue, HTTPS with **TLS 1.3 on the ESP32**
(certificate checks included), the AWS backend and device registration. The modem only carries IP packets (PPP);
its own built-in HTTP/SSL commands are **not** used (old TLS versions, no certificate control).

Items marked **check** are not confirmed yet and must be checked on the real module before relying on them.

## 0. Before anything is powered

| Item | What to check | Why |
|---|---|---|
| Board version | Photo of both sides of the breakout; the version printed on it (looks like V3.8.x) and the pin labels | Blue "MINI" boards differ: supply input, level shifting, power-key wiring (**check**) |
| Supply | A separate 5 V supply that can give **2 A peaks** (3 A better): a bench supply, or the 12/24 V to 5 V buck converter Ahmed mentioned. Short, thick wires | The module draws up to about 2 A in short bursts when it transmits; a weak supply makes it reset (brownout) |
| Never | Power it from the XIAO's 3V3 or 5V pin, or from a USB-TTL adapter's VCC pin. Never put 5 V on a bare SIM900 VBAT pin (about 4 V max) | Brownouts on the ECU, or a damaged module |
| Ground | One common ground between the supply, the modem, and the USB-TTL adapter or the XIAO | UART needs a shared reference |
| Logic level | The SIM900's own UART runs at 2.8 V. Find whether this breakout has level shifting and a logic-level pin (often `VMCU` / `VCC_MCU`) and what voltage it accepts (**check**) | A 3.3 V ESP32 or adapter straight onto 2.8 V pins can damage the module |
| Antenna | GSM antenna screwed on before power | Transmitting without an antenna can damage the RF stage |
| SIM card | Activated, PIN lock **off** (turn it off in a phone first), data allowance, and the line allows **2G (GSM/GPRS)**; size fits the slot. Insert or remove only with power off | `AT+CPIN?` fails with a PIN; calls/SMS working does not prove data works |
| Network | Check that the chosen Egyptian operator still runs 2G where the tests happen, and its **APN** (and any PPP user/password) (**check**) | SIM900A is GSM 900/1800 MHz only: no 3G or 4G. Some SIM900A units are region-locked (**check**) |

## How to talk to the modem without a USB-to-serial adapter: the XIAO AT bridge

The XIAO can stand in for the adapter. The **modem AT bridge** firmware (`ecu/main/modem_bridge_main.cpp`, built on
17 Nov 2026 with 0 warnings; not run yet) forwards what you type in the monitor to the modem on UART2 and prints
the modem's answers. It uses no Wi-Fi, no network and no keys; flashing it does not erase the device key, so the
Wi-Fi report firmware can be flashed back afterwards without registering the board again.

Wiring (after the checks in section 0): XIAO **D3** (TX) -> modem RX, XIAO **D1** (RX) <- modem TX, GND to GND,
through level shifting if the breakout does not have its own. The modem keeps its own 5 V / 2 A supply. Switch
the modem on with its own button (no pin is left for its power key). Nothing on D0 or D2: the stacked GPS uses
them (WAKEUP and RESET).

```bash
cd firmware/ecu
idf.py -B build_modem_bridge -D SDKCONFIG=build_modem_bridge/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.modem_bridge" set-target esp32s3 build
idf.py -B build_modem_bridge -p /dev/cu.usbmodem101 flash monitor | tee ../results/bringup/logs/sim900a_at_$(date +%Y%m%d_%H%M).log
```

Replace the port with the one `ls /dev/cu.usbmodem*` prints. Expected first line: `BRIDGE BEGIN modem AT bridge ...`.
Type a command and press Enter; the bridge prints `> AT` and then the modem's answer. `~baud 115200` changes the
UART speed, `~stats` shows byte counts, Ctrl-] quits. Then run the table in section 1 (SIM-01..SIM-10); with the
bridge, section 1 and section 2 (SIM-11, SIM-12) are the same run. Afterwards flash the report firmware again
(`BRINGUP_CHECKLIST.md` section 4b).

## 1. Laptop only: prove the module, SIM and network (no ESP32)

(With a USB-to-serial adapter. Without one, use the XIAO AT bridge above: same commands, same expected answers.)

Equipment: USB-TTL adapter (3.3 V logic, not RS-232), the 5 V / 2 A supply, the SIM, the antenna.

Wiring: adapter TX -> modem RX, adapter RX <- modem TX, GND to GND, adapter VCC **not connected**. Respect the
logic-level answer from section 0. Serial settings 8N1, no flow control.

Power on with the board's power button (or its power-key pin; on a bare module it is pulled low for about 1 s,
**check** this breakout). Open a serial terminal (for example `screen /dev/cu.usbserial-XXXX 9600`), try 9600 baud
first, then 115200. The module uses auto-baud: send `AT` a few times; missing start-up text is normal.

| ID | Command | Expected answer | If not |
|---|---|---|---|
| SIM-01 | `AT` | `OK` | Nothing: swap TX/RX, other baud, check power LED and power-on |
| SIM-02 | `ATE0` then `AT+CMEE=2` | `OK`, `OK` (no echo, readable errors) | - |
| SIM-03 | `ATI` | `SIM900...` model/revision text, `OK`. Record it | A different model: write down what it is |
| SIM-04 | `AT+CPIN?` | `+CPIN: READY` | `SIM PIN`: turn the PIN off in a phone. `NOT INSERTED`: reseat (power off) |
| SIM-05 | `AT+CSQ` | `+CSQ: <rssi>,<ber>`, rssi 10..31 good, 99 unknown | Low: antenna, position near a window |
| SIM-06 | `AT+CREG?` | `+CREG: 0,1` (home) or `0,5` (roaming) | `0,2` searching (wait), `0,3` denied (SIM/operator/2G not allowed) |
| SIM-07 | `AT+COPS?` | the intended operator's name | Wrong or none: check the SIM and 2G coverage |
| SIM-08 | `AT+CGATT?` (if 0: `AT+CGATT=1`, wait) | `+CGATT: 1` | Data not allowed on the line, no 2G data coverage |
| SIM-09 | `AT+CGDCONT=1,"IP","<APN>"`, then `AT+CGDCONT?` | `OK`, the APN echoed back | Wrong APN: ask the operator |
| SIM-10 | Network LED, watched for 30 s | Fast blink about every 0.8 s = searching; slow blink about every 3 s = registered; faster during data (**check** which LED is the network light) | Stays fast: not registered (back to SIM-06) |

Stop here if SIM-04, SIM-06 or SIM-08 fails: the SIM path cannot work with this SIM, operator or location, and no
firmware work is worth doing until it does.

Optional stronger check (still no ESP32): bring up PPP from the laptop over the adapter (`ATD*99***1#` should answer
`CONNECT`, then PPP frames). `CONNECT` alone does not prove internet; an IP address, DNS and an HTTPS request do.

## 2. ESP32 talks to the modem (AT smoke test on the board)

Pins reserved for the modem in `ecu/main/board_pins.h`: UART2 on **D3** (GPIO4, ESP32 TX -> modem RX) and
**D1** (GPIO2, ESP32 RX <- modem TX). These are the only two pads the stacked L76K GPS leaves free: it uses D0
(WAKEUP, 0 = sleep) and D2 (RESET, 0 = reset) besides D6/D7, so modem traffic on D0 would keep putting the GPS
to sleep. D4/D5 are the IMU, D8-D10 the microSD.

- Logic level through the converter or level pin found in section 0.
- No pin is left for the modem's power key: use the breakout's button. Driving it from the ESP32 later needs a
  pin freed elsewhere (for example the GPS unstacked and wired by hand), and a transistor if the breakout needs it.
- The modem keeps its own 5 V / 2 A supply; only ground and the two UART lines (D3, D1) go to the XIAO.
- Firmware: the AT bridge above (typed commands). An automatic AT check inside the smoke test is a later step.

| ID | Check | Expected |
|---|---|---|
| SIM-11 | ESP32 sends `AT` on UART2 | `OK` within 1 s |
| SIM-12 | SIM-03..SIM-08 from the ESP32 | Same answers as from the laptop |
| SIM-13 | Leave it running 10 minutes, `AT+CSQ` / `AT+CREG?` every 30 s | No ECU or modem resets (no brownout), stays registered |

## 3. Data over PPP, then the real report path

Firmware work (not started; files listed in section 5):

- Add Espressif's `esp_modem` component and lwIP PPP. Espressif lists SIM800 as supported but no SIM900-specific
  device: start with the generic module, and try the SIM800 profile only as an experiment (**check**).
- The modem becomes a third network kind next to `wifi` and `qemu-ethernet` (for example `gprs-ppp`), behind the
  same interface the tested `WifiLink` state machine drives, so delivery, retries and the queue stay unchanged.
- Timeouts: the Wi-Fi join timeout (15 s) and the HTTP timeout (10 s) are too short for 2G. Start with about
  120-180 s for the modem to register and attach, and 60-120 s for an HTTPS request, then measure.

| ID | Check | Expected |
|---|---|---|
| SIM-14 | PPP up | `NET got-ip gprs-ppp ip=...` (or similar), DNS works |
| SIM-15 | Time | SNTP sync over GPRS within the boot limit |
| SIM-16 | HTTPS | TLS 1.3 to the AWS Lambda URL, certificate verified, on the ESP32 |
| SIM-17 | Signed report | First boot 403 `unknown_device` (report kept); after registration (same as the Wi-Fi board) `201`, `DEMO RESULT PASS` |
| SIM-18 | Signal loss | Unscrew the antenna or cover the module, then restore: link drops, report kept, PPP comes back, kept report sent (`201`) |
| SIM-19 | Data use | Bytes per report from the operator's counter (estimate 5-20 KB per report with a fresh TLS 1.3 handshake, **check**) |

## 4. Risks (to tell the mentor)

1. **2G is old.** Slow (tens of kbit/s), and operators are switching 2G off over time; whether and when Egypt's
   operators do is a **check**. A 4G module (for example SIM7600E-H, the one `board_pins.h` already reserves pins for,
   or a Cat-1 module such as A7670) avoids this.
2. **Power.** 2 A bursts need their own supply; brownouts are the most common failure with these modules.
3. **Region lock / firmware.** Some SIM900A units only accept certain networks (**check** with SIM-06 and SIM-08).
4. **Time on the board.** Every new link means new code paths and new tests; the Wi-Fi path is already built and
   ready for Wednesday. The SIM path starts only after the Wi-Fi bring-up, and only if section 1 passes.
5. **Cost.** Each report uses data; budget it with SIM-19.

## 5. Code changes when (and if) section 1 passes

| File | Change |
|---|---|
| `ecu/main/board_pins.h` | Done: named modem pins (UART2 D3 TX / D1 RX; D0/D2 are the stacked GPS), 2.8 V / level note. Later (if a pin is freed): power-key circuit |
| `ecu/main/idf_component.yml`, `CMakeLists.txt`, `Kconfig.projbuild`, `sdkconfig.defaults` | Pinned `esp_modem`, PPP options, link choice (Wi-Fi or modem), APN, cellular timeouts |
| `ecu/main/net_link.h/.cpp` (+ new `modem_link.h/.cpp`) | PPP start/stop, got/lost-IP events for PPP, non-blocking start |
| `ecu/main/modem_bridge_main.cpp` | Done: AT bridge (SIM-01..SIM-12 by hand). Later, `smoke_main.cpp`: automatic AT check |
| `ecu/main/report_demo_main.cpp`, `http_transport.cpp` | Link selection, longer HTTP timeout for the modem |
| `lib/icvsp_board/src/wifi_link.h/.cpp`, `test/test_wifi_link` | Join timeout configurable per link, host tests for it |

Automatic switching between Wi-Fi and the modem at run time is a further step (choosing a route, recovery rules);
the plan above is a build-time choice of one link.
