#!/usr/bin/env bash
# HTTPS checks of the ECU firmware in the ESP32-S3 emulator (BRINGUP_CHECKLIST.md section 4c).
#
# The HTTPS test firmware (build_report_tls) trusts only the test CA from make_test_certs.py and checks
# certificate dates. The backend on the laptop serves https on port 8443 with a different certificate in
# each case, and the same firmware image (same chip, same key) is run against each:
#   (for 1-3 a TLS client on the laptop must give the intended reason: untrusted CA / wrong host / expired)
#   1 untrusted    certificate from a CA the firmware does not know   -> TLS refused, report kept, nothing sent
#   2 wrong_host   test CA, but issued for another host name           -> TLS refused, report kept, nothing sent
#   3 expired      test CA, right host, valid only 2020-2021           -> TLS refused, report kept, nothing sent
#   4 good         test CA, right host, valid; device not registered   -> TLS OK, backend answers 403 unknown_device
#   5 good         after registering the device's public key           -> TLS OK, 201 stored
#   6 tls12_only   good certificate, but the server allows only TLS 1.2 -> TLS refused (the firmware requires
#                  TLS 1.3, FR-SEC-08), report kept, nothing stored although the device is registered
# The laptop check also prints the TLS version the server agreed to (TLSv1.3 in cases 4-5, TLSv1.2 in case 6).
#
# usage: run_tls_checks.sh <out dir for the logs>
#   PY=/path/to/python with the backend requirements (default backend/.venv/bin/python)
set -uo pipefail
command -v qemu-system-xtensa >/dev/null || . ~/esp/esp-idf/export.sh >/dev/null 2>&1
if ! command -v qemu-system-xtensa >/dev/null; then
    for d in ~/.espressif/tools/qemu-xtensa/*/qemu/bin; do [ -x "$d/qemu-system-xtensa" ] && PATH="$d:$PATH"; done
fi
command -v qemu-system-xtensa >/dev/null || { echo "qemu-system-xtensa not found"; exit 1; }

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
FW="$ROOT/firmware"
CERTS="$FW/ecu/tls_test"
IMG="$FW/ecu/build_report_tls/flash_qemu.bin"
OUT="${1:?usage: run_tls_checks.sh <out dir>}"
PY="${PY:-$ROOT/backend/.venv/bin/python}"
[ -f "$IMG" ] || { echo "missing $IMG (build it: checklist 4c)"; exit 1; }
[ -f "$CERTS/good.crt" ] || { echo "missing test certificates (run make_test_certs.py)"; exit 1; }
if lsof -nP -iTCP:8443 -sTCP:LISTEN >/dev/null 2>&1; then echo "port 8443 is in use"; exit 1; fi
mkdir -p "$OUT" && OUT="$(cd "$OUT" && pwd)"  # absolute: the backend is started from backend/
WORK="$(mktemp -d "${TMPDIR:-/tmp}/icvsp_tls.XXXXXX")"
TOKEN="$("$PY" -c 'import secrets; print(secrets.token_hex(16))')"
BACKEND_PID=""
QEMU_PID=""
cp "$IMG" "$WORK/flash.bin"  # one chip for all cases: the key made at first boot stays in its flash
stop_backend() { [ -n "$BACKEND_PID" ] && kill "$BACKEND_PID" 2>/dev/null; BACKEND_PID=""; sleep 1; }
stop_qemu() { [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null; QEMU_PID=""; }
cleanup() { stop_qemu; stop_backend; rm -rf "$WORK"; }
trap cleanup EXIT

start_backend() {  # $1 = certificate stem, $2.. = extra uvicorn options
    local stem="$1"; shift
    ( cd "$ROOT/backend" && exec env -u DATABASE_URL -u SUPABASE_URL -u SUPABASE_JWT_SECRET ICVSP_DB="$WORK/db.sqlite3" \
        ICVSP_ALLOW_ANONYMOUS=1 ICVSP_ADMIN_TOKEN="$TOKEN" "$PY" -m uvicorn --factory icvsp_backend.app:create_app \
        --host 127.0.0.1 --port 8443 --ssl-certfile "$CERTS/$stem.crt" --ssl-keyfile "$CERTS/$stem.key" "$@" \
        >> "$OUT/backend_tls.log" 2>&1 ) &
    BACKEND_PID=$!
    disown "$BACKEND_PID"
    for _ in $(seq 1 40); do nc -z 127.0.0.1 8443 2>/dev/null && return 0; sleep 0.25; done
    echo "backend did not start"; exit 1
}

run_emulator() {  # $1 = log file; runs until the firmware's result line
    local log="$1"
    : > "$log"
    ( exec qemu-system-xtensa -display none -machine esp32s3 -m 8M -drive file="$WORK/flash.bin",if=mtd,format=raw \
        -nic user,model=open_eth -serial "file:$log" < /dev/null > /dev/null 2>&1 ) &
    QEMU_PID=$!
    disown "$QEMU_PID"
    for _ in $(seq 1 400); do grep -q "^DEMO RESULT" "$log" 2>/dev/null && break; sleep 0.5; done
    stop_qemu
    sleep 1
    tr -d '\r' < "$log" > "$log.tmp" && mv "$log.tmp" "$log"
}

verdict=0
probe() {  # TLS handshake from the laptop to 127.0.0.1:8443 with only the test CA trusted; prints ok or the reason
    "$PY" -I - "$CERTS/ca.pem" <<'PYEOF'
import socket, ssl, sys
ctx = ssl.create_default_context(cafile=sys.argv[1])
try:
    with socket.create_connection(("127.0.0.1", 8443), timeout=5) as s, ctx.wrap_socket(s, server_hostname="127.0.0.1") as t:
        print("ok", t.version())
except ssl.SSLCertVerificationError as e:
    print(e.verify_message)
except Exception as e:
    print(type(e).__name__, e)
PYEOF
}
check() {  # $1 case, $2 log, $3 expected pattern in an HTTP line, $4 description
    local got
    got="$(grep -m1 '^HTTP POST' "$2" | sed 's/{.*//')"
    if grep -q "^HTTP POST .*$3" "$2"; then echo "PASS  $1: $4 ($got)"; else echo "FAIL  $1: $4 (got: ${got:-no HTTP line})"; verdict=1; fi
}

: > "$OUT/backend_tls.log"
i=0
for c in untrusted wrong_host expired; do
    i=$((i + 1))
    start_backend "$c"
    echo "# case $i: server certificate '$c' (see run_tls_checks.sh); $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$WORK/h"
    # The firmware only learns "certificate verify failed" (mbedTLS drops the certificate on failure), so the
    # reason is shown from the laptop: Python's TLS client, the same test CA, the same server.
    case $c in
        untrusted) why="self-signed certificate in certificate chain" ;;  # the chain ends in a CA nobody trusts
        wrong_host) why="IP address mismatch" ;;
        expired) why="certificate has expired" ;;
    esac
    reason="$(probe)"
    echo "# laptop check, same CA and server: $reason" >> "$WORK/h"
    case "$reason" in *"$why"*) echo "PASS  $i $c: laptop TLS client gives the intended reason: $reason" ;;
        *) echo "FAIL  $i $c: expected \"$why\", laptop TLS client says: $reason"; verdict=1 ;; esac
    run_emulator "$WORK/serial.log"
    cat "$WORK/h" "$WORK/serial.log" > "$OUT/qemu_tls_${i}_$c.log"
    stop_backend
    L="$OUT/qemu_tls_${i}_$c.log"
    check "$i $c" "$L" "-> no answer" "no answer from the server"
    # ... and it must be the certificate check that failed (mbedTLS -0x2700 = X509 certificate verify failed),
    # not a timeout or refused connection, and the report must still be queued
    if grep -q "mbedtls_ssl_handshake returned -0x2700" "$L"; then echo "PASS  $i $c: firmware refused the certificate (-0x2700)"
    else echo "FAIL  $i $c: no certificate-verification failure in the log"; verdict=1; fi
    if grep -q "^DEMO RESULT .*stored_by_backend=0 .*kept=1 " "$L"; then echo "PASS  $i $c: report kept, nothing stored"
    else echo "FAIL  $i $c: report not kept: $(grep '^DEMO RESULT' "$L")"; verdict=1; fi
done

start_backend good
echo "# case 4: server certificate 'good', device not registered yet; $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$WORK/h"
reason="$(probe)"; echo "# laptop check, same CA and server: $reason" >> "$WORK/h"
[ "$reason" = "ok TLSv1.3" ] && echo "PASS  4 good: laptop TLS client accepts it ($reason)" || { echo "FAIL  4 good: laptop TLS client says: $reason"; verdict=1; }
run_emulator "$WORK/serial.log"
cat "$WORK/h" "$WORK/serial.log" > "$OUT/qemu_tls_4_good_unregistered.log"
check "4 good" "$OUT/qemu_tls_4_good_unregistered.log" "-> 403" "TLS accepted, backend answered (not registered yet)"

ID="$(grep -o 'device=[a-z0-9-]*' "$OUT/qemu_tls_4_good_unregistered.log" | head -1 | cut -d= -f2)"
PK="$(grep -o 'public_key=[0-9a-f]\{64\}' "$OUT/qemu_tls_4_good_unregistered.log" | head -1 | cut -d= -f2)"
echo "register $ID over https (test CA):"
curl -s --cacert "$CERTS/ca.pem" -X POST https://127.0.0.1:8443/devices -H "X-Admin-Token: $TOKEN" \
    -H 'Content-Type: application/json' -d "{\"device_id\":\"$ID\",\"public_key\":\"$PK\",\"vetted\":false}" |
    sed 's/"pairing_code":"[^"]*"/"pairing_code":"****"/'; echo

echo "# case 5: server certificate 'good', device registered; $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$WORK/h"
run_emulator "$WORK/serial.log"
cat "$WORK/h" "$WORK/serial.log" > "$OUT/qemu_tls_5_good.log"
stop_backend
check "5 good" "$OUT/qemu_tls_5_good.log" "-> 201" "TLS accepted, report stored"
grep -q "^DEMO RESULT PASS" "$OUT/qemu_tls_5_good.log" || { echo "FAIL  5 good: no DEMO RESULT PASS"; verdict=1; }

# case 6: same good certificate and the same database (device registered), but the server offers only TLS 1.2
# (uvicorn --ssl-version 5 = ssl.PROTOCOL_TLSv1_2). A firmware that fell back to TLS 1.2 would get 201 here.
start_backend good --ssl-version 5
L="$OUT/qemu_tls_6_tls12_only.log"
echo "# case 6: server certificate 'good', device registered, server allows only TLS 1.2; $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$WORK/h"
reason="$(probe)"; echo "# laptop check, same CA and server: $reason" >> "$WORK/h"
[ "$reason" = "ok TLSv1.2" ] && echo "PASS  6 tls12_only: laptop TLS client gets TLS 1.2 only ($reason)" ||
    { echo "FAIL  6 tls12_only: laptop TLS client says: $reason"; verdict=1; }
run_emulator "$WORK/serial.log"
cat "$WORK/h" "$WORK/serial.log" > "$L"
stop_backend
check "6 tls12_only" "$L" "-> no answer" "no answer: the firmware does not fall back to TLS 1.2"
if grep -q "mbedtls_ssl_handshake returned" "$L" && ! grep -q "mbedtls_ssl_handshake returned -0x2700" "$L"; then
    echo "PASS  6 tls12_only: TLS handshake failed, not a certificate failure ($(grep -o 'mbedtls_ssl_handshake returned -0x[0-9A-Fa-f]*' "$L" | head -1))"
else echo "FAIL  6 tls12_only: no version-related handshake failure in the log"; verdict=1; fi
if grep -q "^DEMO RESULT .*stored_by_backend=0 .*kept=1 " "$L"; then echo "PASS  6 tls12_only: report kept, nothing stored"
else echo "FAIL  6 tls12_only: report not kept: $(grep '^DEMO RESULT' "$L")"; verdict=1; fi
echo
[ $verdict -eq 0 ] && echo "ALL TLS CHECKS PASSED" || echo "SOME TLS CHECKS FAILED"
exit $verdict
