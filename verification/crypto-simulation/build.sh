#!/usr/bin/env bash
# Download verified Monocypher 4.0.3 sources once, then compile C library + C++ ECU.
set -euo pipefail
cd "$(dirname "$0")"
VERSION=4.0.3
SRC_DIR="vendor/monocypher-${VERSION}"
ARCHIVE="vendor/monocypher-${VERSION}.tar.gz"
EXPECTED_SHA512="40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965"
mkdir -p vendor
if [[ ! -d "$SRC_DIR" ]]; then
  if [[ ! -f "$ARCHIVE" ]]; then
    echo "[Build] Downloading Monocypher ${VERSION} from official website..."
    curl -fL --retry 3 "https://monocypher.org/download/monocypher-${VERSION}.tar.gz" -o "$ARCHIVE"
  fi
  python3 - "$ARCHIVE" "$EXPECTED_SHA512" <<'PY'
import hashlib, sys
archive, expected = sys.argv[1:]
with open(archive, 'rb') as f:
    actual = hashlib.file_digest(f, 'sha512').hexdigest()
if actual != expected:
    raise SystemExit('ERROR: Monocypher SHA-512 checksum mismatch. Remove the archive and retry.')
print('[Build] Monocypher SHA-512 verified')
PY
  tar -xzf "$ARCHIVE" -C vendor
fi
mkdir -p build
cc -std=c99 -O2 -Wall -Wextra -I"$SRC_DIR/src" \
   -c "$SRC_DIR/src/monocypher.c" -o build/monocypher.o
cc -std=c99 -O2 -Wall -Wextra -I"$SRC_DIR/src" -I"$SRC_DIR/src/optional" \
   -c "$SRC_DIR/src/optional/monocypher-ed25519.c" -o build/monocypher-ed25519.o
c++ -std=c++17 -O2 -Wall -Wextra -I"$SRC_DIR/src" -I"$SRC_DIR/src/optional" \
   src/ecu_sim.cpp build/monocypher.o build/monocypher-ed25519.o -o build/ecu_sim
echo '[Build] OK: build/ecu_sim'
