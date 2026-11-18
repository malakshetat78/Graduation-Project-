#include "report_signer.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"

static bool valid_id(const char *s)
{
    if (s == NULL || *s == '\0') return false;
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        const unsigned char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              c == '.' || c == ':')) return false;
        if (++len > 64) return false;
    }
    return true;
}

static void to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[2 * i] = hex[bytes[i] >> 4];
        out[2 * i + 1] = hex[bytes[i] & 15];
    }
    out[2 * len] = '\0';
}

static size_t assemble(const char *sender, const char *msg_id,
                       int64_t utc_ms, const char *event_json,
                       uint8_t buf[ICVSP_MAX_SIGNED_BYTES])
{
    if (!valid_id(sender) || !valid_id(msg_id) ||
        event_json == NULL || event_json[0] != '{' || utc_ms <= 0) return 0;

    const size_t sender_len = strlen(sender);
    const size_t msg_len = strlen(msg_id);
    const size_t payload_len = strlen(event_json);
    // sizeof includes the domain separator NUL.
    static const char domain[] = "ICVSP-v1";
    const size_t header_len = sizeof(domain) + sender_len + 1 + msg_len + 1 + 8;
    if (header_len >= ICVSP_MAX_SIGNED_BYTES ||
        payload_len == 0 || payload_len > ICVSP_MAX_SIGNED_BYTES - header_len) return 0;

    size_t pos = 0;
    memcpy(buf + pos, domain, sizeof(domain)); pos += sizeof(domain);
    memcpy(buf + pos, sender, sender_len + 1); pos += sender_len + 1;
    memcpy(buf + pos, msg_id, msg_len + 1); pos += msg_len + 1;
    const uint64_t t = (uint64_t)utc_ms;
    for (int i = 7; i >= 0; --i) buf[pos++] = (uint8_t)(t >> (8 * i));
    memcpy(buf + pos, event_json, payload_len); pos += payload_len;
    return pos;
}

bool icvsp_sign_report_line(const uint8_t secret_key[64], const uint8_t public_key[32],
                            const char *sender, const char *msg_id, int64_t utc_ms,
                            const char *event_json, char *out, size_t capacity)
{
    if (!secret_key || !public_key || !out || capacity == 0) return false;
    out[0] = '\0';
    uint8_t signed_buf[ICVSP_MAX_SIGNED_BYTES];
    uint8_t sig[64] = {0};
    const size_t len = assemble(sender, msg_id, utc_ms, event_json, signed_buf);
    if (!len) return false;
    crypto_ed25519_sign(sig, secret_key, signed_buf, len);
    char public_hex[65], signature_hex[129];
    to_hex(public_key, 32, public_hex);
    to_hex(sig, 64, signature_hex);
    const int n = snprintf(out, capacity,
        "{\"event\":%s,\"sender\":\"%s\",\"msg_id\":\"%s\","
        "\"utc_ms\":%" PRId64 ",\"public_key\":\"%s\",\"signature\":\"%s\"}",
        event_json, sender, msg_id, utc_ms, public_hex, signature_hex);
    crypto_wipe(sig, sizeof(sig));
    crypto_wipe(signed_buf, sizeof(signed_buf));
    if (n < 0 || (size_t)n >= capacity || n > ICVSP_MAX_REPORT_LINE) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool icvsp_verify_report_signature(const uint8_t public_key[32],
                                   const uint8_t signature[64],
                                   const char *sender, const char *msg_id,
                                   int64_t utc_ms, const char *event_json)
{
    if (!public_key || !signature) return false;
    uint8_t buf[ICVSP_MAX_SIGNED_BYTES];
    const size_t len = assemble(sender, msg_id, utc_ms, event_json, buf);
    if (!len) return false;
    const int result = crypto_ed25519_check(signature, public_key, buf, len);
    crypto_wipe(buf, sizeof(buf));
    return result == 0;
}
