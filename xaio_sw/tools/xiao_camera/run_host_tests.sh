#!/usr/bin/env bash
# Host checks for the T-012 camera bring-up. They run on a computer (macOS or Linux) with no board.
# They test the JPEG check, the Base64 encoder and the log extractor. They are NOT camera evidence:
# the camera itself can only be tested on the physical XIAO ESP32-S3 Sense.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../arduino/ICVSP_ECU/src"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cxx="${CXX:-c++}"

echo "== 1. JPEG check and Base64 unit tests (C++)"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$src" "$here/test_jpeg_check.cpp" "$src/core/jpeg_check.cpp" -o "$work/test_jpeg_check"
"$work/test_jpeg_check"

echo "== 2. Log extractor unit tests (Python)"
(cd "$here" && python3 -m unittest test_extract_jpegs.py)

echo "== 3. Round trip: real JPEG -> firmware Base64 format -> extract_jpegs.py -> same bytes"
sample="${1:-}"
if [ -z "$sample" ]; then
    if command -v sips >/dev/null 2>&1; then
        # macOS: make a real 320x240 JPEG from a system image.
        sips -s format jpeg -z 240 320 /System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/GenericDocumentIcon.icns \
            --out "$work/sample.jpg" >/dev/null 2>&1 || true
    fi
    [ -s "$work/sample.jpg" ] && sample="$work/sample.jpg"
fi
if [ -z "$sample" ]; then
    echo "SKIP  no sample JPEG (pass one as the first argument: ./run_host_tests.sh photo.jpg)"
else
    "$cxx" -std=c++17 -Wall -Wextra -Werror -I"$src" "$here/sample_log.cpp" "$src/core/jpeg_check.cpp" -o "$work/sample_log"
    "$work/sample_log" "$sample" > "$work/log.txt"
    python3 "$here/extract_jpegs.py" "$work/log.txt" --out "$work/out" > "$work/summary.md"
    if cmp -s "$sample" "$work/out/01_HOST_sample.jpg"; then
        echo "PASS  extracted file is byte-for-byte the original ($(wc -c < "$sample" | tr -d ' ') bytes)"
    else
        echo "FAIL  extracted file differs from the original"; cat "$work/summary.md"; exit 1
    fi
fi
echo "== host checks passed (host only, not hardware evidence)"
