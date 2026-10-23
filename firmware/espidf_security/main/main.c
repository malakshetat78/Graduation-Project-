#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "key_storage.h"
#include "report_signer.h"

void app_main(void)
{
    icvsp_key_pair_t keys = {0};
    uint8_t signature[64] = {0};

    static const uint8_t message[] = "Pothole detected";
    static const uint8_t modified_message[] = "No pothole detected";

    puts("ICVSP - ESP32-S3 Ed25519 / Backend Contract DEMO");

    if (!icvsp_load_or_create_keys(&keys)) {
        puts("FAIL: Device key initialization failed (fail closed).");
        return;
    }
    crypto_ed25519_sign(signature, keys.secret_key, message, sizeof(message) - 1);
    if (crypto_ed25519_check(signature, keys.public_key, message, sizeof(message) - 1) != 0) {
        puts("FAIL: Valid Ed25519 signature rejected.");
        goto cleanup;
    }
    puts("PASS: Original Ed25519 signature verified.");

    if (crypto_ed25519_check(signature, keys.public_key, modified_message, sizeof(modified_message) - 1) == 0) {
        puts("FAIL: Tampered message accepted.");
        goto cleanup;
    }
    puts("PASS: Tampered message rejected.");

    // Development-only schema v2 example. It uses historical DEMO time,
    // coordinates and a sample boot/sequence. NEVER relay this as a live hazard.
    // Production requires: real GNSS time and position, a boot identifier and
    // sequence with robust persistent/replay semantics, and device enrollment.
    const char *demo_event =
        "{\"schema_version\":2,\"event_id\":\"vehicle_001-1-1\","
        "\"device_id\":\"vehicle_001\",\"boot_id\":1,\"sequence\":1,"
        "\"type\":\"pothole\",\"detected_at\":\"2026-09-21T14:13:40.000Z\","
        "\"gps_valid\":true,\"latitude\":30.044400,\"longitude\":31.236473,"
        "\"position_accuracy_m\":3.5,\"speed_kmh\":13.4,\"heading_deg\":90.0,"
        "\"severity\":\"high\",\"detection_confidence\":0.98,"
        "\"model\":\"imu-v0\"}";
    char report_line[ICVSP_MAX_REPORT_LINE + 1];
    if (icvsp_sign_report_line(keys.secret_key, keys.public_key,
                               "vehicle_001", "vehicle_001-1-1",
                               INT64_C(1790000020000), demo_event,
                               report_line, sizeof(report_line))) {
        puts("DEMO_SIGNED_REPORT_START");
        puts(report_line);
        puts("DEMO_SIGNED_REPORT_END");
        puts("PASS: Schema-v2 report signed using backend byte contract.");
    } else {
        puts("FAIL: Cannot assemble signed schema-v2 report.");
    }

cleanup:
    crypto_wipe(signature, sizeof(signature));
    crypto_wipe(keys.secret_key, sizeof(keys.secret_key));
}
