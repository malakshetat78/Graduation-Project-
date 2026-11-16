// ROVO Phase 1: laptop-only ECU simulator.
// Packet v1 (big-endian):
//   Header: magic[4] + version[1] + device_id[16] + nonce[24] + ciphertext_len[2] = 47
//   Ciphertext[33] + Poly1305 tag[16] + Ed25519 signature[64] = 113
//   Total = 160 bytes.
// Signature covers header || ciphertext || tag. Header is also AEAD AAD.
// Demo only. Not ESP32 firmware; no key provisioning or anti-replay database yet.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
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
constexpr std::size_t kMessageSize = 33;
constexpr std::size_t kHeaderSize = 47;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kSignatureSize = 64;
constexpr std::size_t kPacketSize = kHeaderSize + kMessageSize + kTagSize + kSignatureSize;
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
    s.reserve(n*2);
    for (std::size_t i=0; i<n; ++i) {
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
        auto symmetric_key = read_exact(keys_dir + "/enc_key.bin", 32);
        auto seed = read_exact(keys_dir + "/sign_seed.bin", 32);
        auto expected_public = read_exact(keys_dir + "/sign_pub.bin", 32);

        std::array<Byte,64> ed_private{};
        std::array<Byte,32> ed_public{};
        crypto_ed25519_key_pair(ed_private.data(), ed_public.data(), seed.data());
        // Monocypher wipes seed during keypair creation; fail safely on mismatched keys.
        if (std::memcmp(ed_public.data(), expected_public.data(), 32) != 0) {
            throw std::runtime_error("Ed25519 public key mismatch; check the key files");
        }

        std::array<Byte,16> message_id{};
        std::array<Byte,24> nonce{};
        secure_random(message_id.data(), message_id.size());
        secure_random(nonce.data(), nonce.size());
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto timestamp_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());

        // Example GNSS coordinates: 30.0444200, 31.2357120 (decimal degrees).
        // Latitude/longitude are signed int32 multiplied by 10^7.
        const std::int32_t latitude_e7 = 300444200;
        const std::int32_t longitude_e7 = 312357120;
        const Byte hazard_type = 1;  // 1=Pothole, 2=Speed bump

        std::vector<Byte> plaintext;
        append_bytes(plaintext, message_id.data(), message_id.size());
        append_u64(plaintext, timestamp_ms);
        append_u32(plaintext, static_cast<std::uint32_t>(latitude_e7));
        append_u32(plaintext, static_cast<std::uint32_t>(longitude_e7));
        plaintext.push_back(hazard_type);
        if (plaintext.size() != kMessageSize) throw std::runtime_error("Plaintext size bug");

        std::vector<Byte> header;
        header.insert(header.end(), {'R','O','V','O'});
        header.push_back(1);  // version
        append_bytes(header, kDeviceId.data(), kDeviceId.size());
        append_bytes(header, nonce.data(), nonce.size());
        append_u16(header, static_cast<std::uint16_t>(plaintext.size()));
        if (header.size() != kHeaderSize) throw std::runtime_error("Header size bug");

        std::vector<Byte> ciphertext(plaintext.size());
        std::array<Byte, kTagSize> tag{};
        crypto_aead_lock(ciphertext.data(), tag.data(), symmetric_key.data(), nonce.data(),
                         header.data(), header.size(), plaintext.data(), plaintext.size());

        std::vector<Byte> signed_data = header;
        append_bytes(signed_data, ciphertext.data(), ciphertext.size());
        append_bytes(signed_data, tag.data(), tag.size());
        std::array<Byte, kSignatureSize> signature{};
        crypto_ed25519_sign(signature.data(), ed_private.data(), signed_data.data(), signed_data.size());

        std::vector<Byte> packet = signed_data;
        append_bytes(packet, signature.data(), signature.size());
        if (packet.size() != kPacketSize) throw std::runtime_error("Packet size bug");
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(packet.data()), packet.size())) {
            throw std::runtime_error("Cannot write " + output_path);
        }
        output.close();

        // Reduce accidental persistence of secrets in memory in this demo.
        crypto_wipe(symmetric_key.data(), symmetric_key.size());
        crypto_wipe(ed_private.data(), ed_private.size());
        crypto_wipe(seed.data(), seed.size());
        std::cout << "[ECU C++] packet written: " << output_path << "\n";
        std::cout << "[ECU C++] original=33, header=47, tag=16, signature=64, total="
                  << packet.size() << " bytes\n";
        std::cout << "[ECU C++] message_id=" << to_hex(message_id.data(), message_id.size()) << "\n";
        std::cout << "[ECU C++] signed and encrypted successfully\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[ECU C++] ERROR: " << ex.what() << "\n";
        return 1;
    }
}
