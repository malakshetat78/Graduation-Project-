#include "jpeg_check.h"

namespace icvsp {

namespace {

bool is_frame_header(uint8_t marker)
{
    // Baseline, extended sequential and progressive frame headers (the camera produces baseline SOF0).
    return marker == 0xC0 || marker == 0xC1 || marker == 0xC2;
}

}  // namespace

JpegInfo inspect_jpeg(const uint8_t* data, size_t len, size_t trailing_window)
{
    JpegInfo info;
    if (data == nullptr || len < 4) {
        return info;
    }
    info.has_start = data[0] == 0xFF && data[1] == 0xD8;

    // End marker: search backwards over the last trailing_window bytes.
    const size_t stop = len > trailing_window ? len - trailing_window : 0;
    for (size_t i = len - 1; i > stop; --i) {
        if (data[i - 1] == 0xFF && data[i] == 0xD9) {
            info.has_end = true;
            info.end_offset = i + 1;
            break;
        }
    }

    // Walk the marker segments after FF D8 through the frame header (SOF) up to the scan header (SOS).
    size_t pos = 2;
    while (info.has_start && pos + 4 <= len) {
        if (data[pos] != 0xFF) {
            break;  // not a marker: the header is malformed
        }
        const uint8_t marker = data[pos + 1];
        if (marker == 0xFF) {
            ++pos;  // fill byte
            continue;
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            pos += 2;  // markers without a length field
            continue;
        }
        const size_t seg_len = (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
        if (seg_len < 2 || pos + 2 + seg_len > len) {
            break;
        }
        if (is_frame_header(marker)) {
            // SOF: length, precision (8), height, width, component count, then 3 bytes per component.
            const uint8_t components = seg_len >= 8 ? data[pos + 9] : 0;
            if (info.width != 0 || data[pos + 4] != 8 || components < 1 || components > 4 ||
                seg_len != 8u + 3u * components) {
                info.width = info.height = 0;
                break;  // malformed or second frame header
            }
            info.height = static_cast<uint16_t>((data[pos + 5] << 8) | data[pos + 6]);
            info.width = static_cast<uint16_t>((data[pos + 7] << 8) | data[pos + 8]);
        } else if (marker == 0xDA) {
            // SOS: length, component count, 2 bytes per component, then 3 bytes of spectral selection.
            const uint8_t components = seg_len >= 3 ? data[pos + 4] : 0;
            if (info.width != 0 && components >= 1 && components <= 4 && seg_len == 6u + 2u * components) {
                info.has_scan = true;
                info.scan_offset = pos + 2 + seg_len;
            }
            break;  // compressed image data starts here
        }
        pos += 2 + seg_len;
    }
    return info;
}

size_t base64_encode(const uint8_t* data, size_t len, char* out, size_t out_cap)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t need = base64_size(len);
    if (out == nullptr || out_cap < need + 1) {
        return 0;
    }
    size_t o = 0;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (data[i + 1] << 8) | data[i + 2];
        out[o++] = kAlphabet[(v >> 18) & 63];
        out[o++] = kAlphabet[(v >> 12) & 63];
        out[o++] = kAlphabet[(v >> 6) & 63];
        out[o++] = kAlphabet[v & 63];
    }
    if (i < len) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) {
            v |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        out[o++] = kAlphabet[(v >> 18) & 63];
        out[o++] = kAlphabet[(v >> 12) & 63];
        out[o++] = i + 1 < len ? kAlphabet[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

}  // namespace icvsp
