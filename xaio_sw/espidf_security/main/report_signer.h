#ifndef ICVSP_REPORT_SIGNER_H
#define ICVSP_REPORT_SIGNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Backend contract: signed bytes = "ICVSP-v1" NUL sender NUL msg_id NUL
//                       signed utc_ms int64 big-endian + exact event JSON.
// The outer JSON MUST keep the "event" member first, followed by "sender".
#define ICVSP_MAX_SIGNED_BYTES 1024
#define ICVSP_MAX_REPORT_LINE 2048

// event_json must be valid, compact UTF-8 JSON, built by the event builder
// (schema_version:2); its exact bytes are signed and transmitted unchanged.
// sender and msg_id accept ASCII letters/numbers, '-', '_', '.', ':' only.
// out receives a NUL-terminated JSON report ready to forward via BLE;
// returns false on invalid identifiers, length, or insufficient buffer.
// Neither this function nor the Flutter relay may reserialize event_json.
bool icvsp_sign_report_line(
    const uint8_t secret_key[64],
    const uint8_t public_key[32],
    const char *sender, const char *msg_id,
    int64_t utc_ms, const char *event_json,
    char *out, size_t out_capacity);

// Verify the exact signed data, mainly for a firmware smoke test.
bool icvsp_verify_report_signature(
    const uint8_t public_key[32], const uint8_t signature[64],
    const char *sender, const char *msg_id,
    int64_t utc_ms, const char *event_json);

#endif
