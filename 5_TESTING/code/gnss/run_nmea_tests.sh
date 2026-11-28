#!/usr/bin/env bash
# Laptop tests for the GNSS NMEA parser used by the firmware (1_XAIO_SW/code/components/icvsp_board/nmea.cpp).
# Runs on a computer (macOS or Linux, C++17 compiler); not hardware evidence.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
comp="$here/../../../1_XAIO_SW/code/components"
out="$(mktemp -d)/test_nmea"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -I"$here" -I"$comp/icvsp_board" -I"$comp/icvsp_core" \
    "$here/../device_logic/test/test_nmea/test_main.cpp" "$comp/icvsp_board/nmea.cpp" -o "$out"
"$out"
