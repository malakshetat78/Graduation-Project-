// ROVO standalone laptop simulator, signed packet v2; NOT ICVSP production firmware.
// Header: magic[4] + version[1] + device_id[16] + payload_len[2] = 23 bytes.
// Payload: message_id[16] + timestamp_ms[8] + lat_e7[4] + lon_e7[4] + hazard[1] = 33.
// Ed25519 signature[64] covers header || payload. Total packet = 120 bytes.
// No XChaCha20 application-level encryption; HTTPS/TLS for mobile->server transport.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#elif !defined(__APPLE__)
#error "Secure random generator is implemented for macOS and Linux only"
#endif

#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace {
using Byte = std::uint8_t;
constexpr std::size_t kPayloadSize = 33;
constexpr std::size_t kHeaderSize = 23;
constexpr std::size_t kSignatureSize = 64;
constexpr std::size_t kPacketSize = kHeaderSize + kPayloadSize + kSignatureSize;
constexpr std::array<Byte,16> kDeviceId = {'R','O','V','O','-','D','E','M','O','-','E','C','U','-','0','1'};

void secure_random(Byte* dest, std::size_t count) {
#if defined(__APPLE__)
    arc4random_buf(dest, count);
#elif defined(__linux__)
    std::size_t done = 0;
    while (done < count) {
        ssize_t got = getrandom(dest + done, count - done, 0);
        if (got == -1 && errno == EINTR) continue;
        if (got <= 0) throw std::runtime_error("getrandom() failed");
        done += static_cast<std::size_t>(got);
    }
#endif
}

std::vector<Byte> read_exact(const std::string& filename, std::size_t expected) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read " + filename);
    std::vector<Byte> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.size() != expected) throw std::runtime_error("Wrong key size in " + filename);
    return buffer;
}
void append_u16(std::vector<Byte>& out, std::uint16_t v) {
    out.push_back(static_cast<Byte>(v >> 8));
    out.push_back(static_cast<Byte>(v));
}
void append_u32(std::vector<Byte>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<Byte>(v >> s));
}
void append_u64(std::vector<Byte>& out, std::uint64_t v) {
    for (int s = 56; s >= 0; s -= 8) out.push_back(static_cast<Byte>(v >> s));
}
void append_bytes(std::vector<Byte>& out, const Byte* bytes, std::size_t len) {
    out.insert(out.end(), bytes, bytes + len);
}
std::string to_hex(const Byte* p, std::size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 15]);
    }
    return s;
}
}

int main(int argc, char** argv) {
    const std::string keys_dir = argc > 1 ? argv[1] : "keys";
    const std::string output_path = argc > 2 ? argv[2] : "packet.bin";
    try {
        auto seed = read_exact(keys_dir + "/sign_seed.bin", 32);
        auto expected_public = read_exact(keys_dir + "/sign_pub.bin", 32);
        std::array<Byte,64> ed_private{};
        std::array<Byte,32> ed_public{};
        crypto_ed25519_key_pair(ed_private.data(), ed_public.data(), seed.data());
        if (std::memcmp(ed_public.data(), expected_public.data(), 32) != 0) {
            throw std::runtime_error("Ed25519 public key mismatch; check key files");
        }
        std::array<Byte,16> message_id{};
        secure_random(message_id.data(), message_id.size());
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto timestamp_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        // Demo-only hard-coded GNSS data: signed int32 coordinates scaled by 10^7.
        const std::int32_t latitude_e7 = 300444200;
        const std::int32_t longitude_e7 = 312357120;
        const Byte hazard_type = 1;
        std::vector<Byte> payload;
        append_bytes(payload, message_id.data(), message_id.size());
        append_u64(payload, timestamp_ms);
        append_u32(payload, static_cast<std::uint32_t>(latitude_e7));
        append_u32(payload, static_cast<std::uint32_t>(longitude_e7));
        payload.push_back(hazard_type);
        if (payload.size() != kPayloadSize) throw std::runtime_error("Payload size mismatch");
        std::vector<Byte> signed_data;
        signed_data.insert(signed_data.end(), {'R','O','V','O'});
        signed_data.push_back(2);  // signed-only packet v2
        append_bytes(signed_data, kDeviceId.data(), kDeviceId.size());
        append_u16(signed_data, static_cast<std::uint16_t>(payload.size()));
        if (signed_data.size() != kHeaderSize) throw std::runtime_error("Header size mismatch");
        append_bytes(signed_data, payload.data(), payload.size());
        std::array<Byte,kSignatureSize> signature{};
        crypto_ed25519_sign(signature.data(), ed_private.data(), signed_data.data(), signed_data.size());
        std::vector<Byte> packet = signed_data;
        append_bytes(packet, signature.data(), signature.size());
        if (packet.size() != kPacketSize) throw std::runtime_error("Packet size mismatch");
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(packet.data()), packet.size())) {
            throw std::runtime_error("Cannot write " + output_path);
        }
        output.close();
        crypto_wipe(seed.data(), seed.size());
        crypto_wipe(ed_private.data(), ed_private.size());
        std::cout << "[ECU C++] packet written: " << output_path << "\n";
        std::cout << "[ECU C++] payload=33, header=23, signature=64, total="
                  << packet.size() << " bytes\n";
        std::cout << "[ECU C++] message_id=" << to_hex(message_id.data(), message_id.size()) << "\n";
        std::cout << "[ECU C++] Ed25519 signed successfully (no message encryption)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[ECU C++] ERROR: " << ex.what() << "\n";
        return 1;
    }
}
