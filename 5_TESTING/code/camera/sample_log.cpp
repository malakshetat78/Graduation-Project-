// Host tool: prints a .jpg file the way the camera bring-up firmware prints a sample photo
// (same icvsp::base64_encode, same 76-character lines, same BEGIN/END lines as camera_test.cpp dump_jpeg).
// run_host_tests.sh feeds the output to extract_jpegs.py and compares the result with the original file.
// This is a host check of the encoding path only. It is not evidence from the camera.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "jpeg_check.h"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: sample_log image.jpg\n");
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const icvsp::JpegInfo info = icvsp::inspect_jpeg(jpeg.data(), jpeg.size());
    if (!info.valid()) {
        std::fprintf(stderr, "%s is not a complete JPEG\n", argv[1]);
        return 1;
    }
    std::vector<char> text(icvsp::base64_size(jpeg.size()) + 1);
    const size_t n = icvsp::base64_encode(jpeg.data(), jpeg.size(), text.data(), text.size());
    std::printf("CAM BEGIN T-012 camera bring-up test (HOST SAMPLE, not from the camera)\n");
    std::printf("-----BEGIN ICVSP JPEG name=HOST_sample width=%u height=%u bytes=%zu lighting=\"host file\"-----\n",
                info.width, info.height, jpeg.size());
    for (size_t pos = 0; pos < n; pos += 76) {
        std::printf("%.*s\n", static_cast<int>(n - pos < 76 ? n - pos : 76), text.data() + pos);
    }
    std::printf("-----END ICVSP JPEG-----\nCAM RESULT pass=0 fail=0\n");
    return 0;
}
