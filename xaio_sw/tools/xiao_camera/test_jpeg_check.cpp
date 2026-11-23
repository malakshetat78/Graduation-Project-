// Host unit test for xaio_sw/arduino/ICVSP_ECU/src/core/jpeg_check.cpp (the JPEG check and Base64 encoder the
// camera bring-up test runs on the board). Runs on a computer, not on the XIAO. See README.md.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/jpeg_check.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// Smallest structure the checker needs: FF D8, APP0, a SOF0 header with the given size, an SOS header for
// three components, a few bytes of image data, FF D9. Not decodable: only the structure is real.
std::vector<uint8_t> fake_jpeg(uint16_t width, uint16_t height, size_t padding_after_end = 0)
{
    std::vector<uint8_t> j = {0xFF, 0xD8,
                              0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
                              0xFF, 0xC0, 0x00, 0x11, 0x08,
                              static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height & 0xFF),
                              static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width & 0xFF),
                              0x03, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1,
                              0xFF, 0xDA, 0x00, 0x0C, 0x03, 1, 0x00, 2, 0x11, 3, 0x11, 0x00, 0x3F, 0x00,
                              0x12, 0x34, 0x56,
                              0xFF, 0xD9};
    j.insert(j.end(), padding_after_end, 0x00);
    return j;
}

std::string b64(const std::string& in)
{
    std::vector<char> out(icvsp::base64_size(in.size()) + 1);
    icvsp::base64_encode(reinterpret_cast<const uint8_t*>(in.data()), in.size(), out.data(), out.size());
    return out.data();
}

}  // namespace

int main()
{
    const auto qvga = fake_jpeg(320, 240);
    const auto info = icvsp::inspect_jpeg(qvga.data(), qvga.size());
    check(info.valid(), "QVGA frame is valid");
    check(info.width == 320 && info.height == 240, "QVGA size read from the frame header");
    check(info.end_offset == qvga.size(), "end marker position");

    const auto padded = fake_jpeg(640, 480, 20);
    const auto pinfo = icvsp::inspect_jpeg(padded.data(), padded.size());
    check(pinfo.valid() && pinfo.width == 640 && pinfo.height == 480, "padding after the end marker is accepted");

    const auto far_padded = fake_jpeg(640, 480, 200);
    check(!icvsp::inspect_jpeg(far_padded.data(), far_padded.size()).has_end,
          "end marker more than 64 bytes before the end is rejected");

    auto truncated = qvga;
    truncated.resize(truncated.size() - 2);
    check(!icvsp::inspect_jpeg(truncated.data(), truncated.size()).valid(), "frame cut before FF D9 is invalid");

    auto no_start = qvga;
    no_start[1] = 0x00;
    check(!icvsp::inspect_jpeg(no_start.data(), no_start.size()).valid(), "frame without FF D8 is invalid");

    std::vector<uint8_t> no_sof = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x00, 0xFF, 0xD9};
    const auto ninfo = icvsp::inspect_jpeg(no_sof.data(), no_sof.size());
    check(!ninfo.valid() && ninfo.has_start && ninfo.has_end, "frame without a size header is invalid");

    std::vector<uint8_t> bad_len = {0xFF, 0xD8, 0xFF, 0xC0, 0x7F, 0xFF, 0x08, 0xFF, 0xD9};
    check(!icvsp::inspect_jpeg(bad_len.data(), bad_len.size()).valid(), "segment longer than the buffer is invalid");

    // Codex review probe: start marker, a 7-byte SOF (too short for its components), zeros, end marker.
    std::vector<uint8_t> short_sof = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x07, 0x08, 0x00, 0xF0, 0x01, 0x40};
    short_sof.insert(short_sof.end(), 500, 0x00);
    short_sof.insert(short_sof.end(), {0xFF, 0xD9});
    check(!icvsp::inspect_jpeg(short_sof.data(), short_sof.size()).valid(), "SOF too short for its components is invalid");

    auto no_scan = qvga;  // replace the SOS marker with a COM marker of the same length
    no_scan[40] = 0xFE;
    const auto sinfo = icvsp::inspect_jpeg(no_scan.data(), no_scan.size());
    check(no_scan[39] == 0xFF && !sinfo.valid() && sinfo.width == 320 && !sinfo.has_scan,
          "frame without a scan header is invalid");

    auto bad_scan = qvga;  // SOS says 3 components but its length is for 2
    bad_scan[42] = 0x0A;
    check(!icvsp::inspect_jpeg(bad_scan.data(), bad_scan.size()).valid(), "scan header with a wrong length is invalid");

    std::vector<uint8_t> no_data(qvga.begin(), qvga.begin() + 53);  // SOS header, then FF D9 straight away
    no_data.insert(no_data.end(), {0xFF, 0xD9});
    check(!icvsp::inspect_jpeg(no_data.data(), no_data.size()).valid(), "no image data after the scan header is invalid");

    auto two_bit = qvga;  // precision must be 8 bits
    two_bit[24] = 0x0C;
    check(!icvsp::inspect_jpeg(two_bit.data(), two_bit.size()).valid(), "12-bit precision frame header is invalid");

    check(!icvsp::inspect_jpeg(nullptr, 0).valid(), "empty buffer is invalid");
    std::vector<uint8_t> zeros(1000, 0);
    check(!icvsp::inspect_jpeg(zeros.data(), zeros.size()).valid(), "all-zero buffer is invalid");

    check(b64("") == "", "Base64 of nothing");
    check(b64("f") == "Zg==", "Base64 one byte");
    check(b64("fo") == "Zm8=", "Base64 two bytes");
    check(b64("foo") == "Zm9v", "Base64 three bytes");
    check(b64("foobar") == "Zm9vYmFy", "Base64 six bytes (RFC 4648 example)");
    char small[4];
    check(icvsp::base64_encode(reinterpret_cast<const uint8_t*>("foo"), 3, small, sizeof small) == 0,
          "Base64 refuses an output buffer that is too small");

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
