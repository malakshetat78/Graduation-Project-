#pragma once
// Checks that a camera frame is a complete JPEG and reads its size from the frame header.
// Plain C++ with no ESP-IDF calls, so the same code is unit tested on a computer (5_TESTING/code/xiao_camera).
#include <cstddef>
#include <cstdint>

namespace icvsp {

struct JpegInfo {
    bool has_start = false;   // starts with the JPEG start marker FF D8
    bool has_end = false;     // FF D9 end marker found near the end of the buffer
    size_t end_offset = 0;    // bytes up to and including FF D9
    uint16_t width = 0;       // from a well-formed SOF0/SOF1/SOF2 frame header, 0 if not found
    uint16_t height = 0;
    bool has_scan = false;    // a well-formed SOS (start of scan) header follows the frame header
    size_t scan_offset = 0;   // first byte of the compressed image data
    // Structural check only: markers, header lengths and image data between SOS and FF D9. It does not decode
    // the picture, so the extracted photos must still be opened (extract_jpegs.py decodes them with Pillow).
    bool valid() const
    {
        return has_start && has_end && width > 0 && height > 0 && has_scan && end_offset > scan_offset + 2;
    }
};

// Looks at the first bytes for FF D8, the frame header and the scan header, and at the last trailing_window
// bytes for FF D9 (the camera driver can leave a few padding bytes after the end marker).
JpegInfo inspect_jpeg(const uint8_t* data, size_t len, size_t trailing_window = 64);

// Standard Base64 (RFC 4648) without line breaks. out_cap must be at least base64_size(len) + 1.
// Returns the number of characters written (not counting the final '\0'), or 0 if out_cap is too small.
constexpr size_t base64_size(size_t len) { return ((len + 2) / 3) * 4; }
size_t base64_encode(const uint8_t* data, size_t len, char* out, size_t out_cap);

}  // namespace icvsp
