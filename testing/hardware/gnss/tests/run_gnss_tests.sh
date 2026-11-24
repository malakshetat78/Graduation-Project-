#!/bin/sh
set -eu

cd "$(dirname "$0")"
repo_root="$(cd ../../../.. && pwd)"
sketch="$repo_root/xaio_sw/arduino/ICVSP_ECU"

build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

for enabled in 0 1; do
    "${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror \
        -Wno-unused-variable \
        -fsanitize=undefined -fno-omit-frame-pointer \
        -DCONFIG_ICVSP_GNSS_SOFTWARE_TESTS="$enabled" \
        -Istubs -I"$sketch/src/app" \
        gnss_software_test.cpp "$sketch/src/board/nmea.cpp" \
        -o "$build_dir/gnss-test"
    "$build_dir/gnss-test"
done
