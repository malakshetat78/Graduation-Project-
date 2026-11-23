# Optional: build from Terminal instead of the Arduino IDE (same result as the IDE's Verify / Upload).
# Uses the arduino-cli that ships inside the Arduino IDE app.
#   make build                                  compile only (checks every .cpp builds and links)
#   make upload PORT=/dev/cu.usbmodem101        compile and send to the board
#   make monitor PORT=/dev/cu.usbmodem101       show the board output (quit with Ctrl-C)
# Which program is built (smoke test or report firmware) is set in config.h (ICVSP_APP).

CLI  ?= "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN ?= esp32:esp32:XIAO_ESP32S3:PSRAM=opi,EraseFlash=none
PORT ?= /dev/cu.usbmodem101

.PHONY: build upload monitor
build:
	$(CLI) compile --fqbn $(FQBN) .

upload:
	$(CLI) compile --fqbn $(FQBN) --upload --port $(PORT) .

monitor:
	$(CLI) monitor --port $(PORT) --config baudrate=115200
