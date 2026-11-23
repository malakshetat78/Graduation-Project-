#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
for enabled in 0 1; do
    "${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -Wno-unused-variable \
        -fsanitize=undefined -fno-omit-frame-pointer \
        -DCONFIG_ICVSP_GNSS_SOFTWARE_TESTS="$enabled" -Itests/stubs \
        tests/gnss_software_test.cpp src/board/nmea.cpp -o "$build_dir/gnss-test"
    "$build_dir/gnss-test"
done
