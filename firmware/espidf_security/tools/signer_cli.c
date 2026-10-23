/* Host-only test driver for the REAL ESP-IDF report_signer.c source.
 * NOT firmware, NOT a live sensor report, NOT a provisioned private key.
 * Reads event JSON from stdin, signs it using an explicitly non-production seed.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "report_signer.h"

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: signer_cli sender msg_id utc_ms < event.json\n");
        return 2;
    }
    char event[900];
    if (!fgets(event, sizeof(event), stdin)) return 2;
    size_t len = strlen(event);
    while (len && (event[len-1] == '\n' || event[len-1] == '\r'))
        event[--len] = 0;
    char *end = NULL;
    int64_t utc_ms = (int64_t)strtoll(argv[3], &end, 10);
    if (!end || *end != '\0' || utc_ms <= 0) return 2;

    uint8_t seed[32], secret[64], public[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)i;
    crypto_ed25519_key_pair(secret, public, seed);
    crypto_wipe(seed, sizeof(seed));

    char output[ICVSP_MAX_REPORT_LINE+1];
    int ok = icvsp_sign_report_line(secret, public, argv[1], argv[2],
                                    utc_ms, event, output, sizeof(output));
    crypto_wipe(secret, sizeof(secret));
    if (!ok) {
        fprintf(stderr, "failed to sign report\n");
        return 1;
    }
    puts(output);
    return 0;
}
