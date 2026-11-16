// Each ECU's own key: made once on the chip, kept across resets, different per board (proposed ECU-identity).
#include <unity.h>

#include <cstring>

#include "device_identity.h"
#include "signer.h"

using namespace icvsp;

namespace {

struct FakeStore : SecretStore {
    bool has = false, corrupt = false, fail_save = false;
    uint8_t stored[32] = {};
    int saves = 0;
    Read load_seed(uint8_t seed[32]) override {
        if (corrupt) return Read::Corrupt;
        if (!has) return Read::Absent;
        std::memcpy(seed, stored, 32);
        return Read::Ok;
    }
    bool save_seed(const uint8_t seed[32]) override {
        ++saves;
        if (fail_save) return false;
        std::memcpy(stored, seed, 32);
        has = true;
        return true;
    }
};

// Deterministic "random" sources for the tests (the board uses the hardware RNG).
uint32_t g_state = 1;
void counter_random(void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) {
        g_state = g_state * 1103515245u + 12345u;
        p[i] = static_cast<uint8_t>(g_state >> 16);
    }
}
void zero_random(void* buf, size_t len) { std::memset(buf, 0, len); }
void stuck_random(void* buf, size_t len) { std::memset(buf, 0x5a, len); }

void public_key_of(const uint8_t seed[32], uint8_t pk[32]) {
    uint8_t s[32], sk[64];
    std::memcpy(s, seed, 32);
    keypair_from_seed(s, sk, pk);
}

}  // namespace

void setUp() { g_state = 1; }
void tearDown() {}

void test_first_boot_creates_and_stores_a_key_later_boots_reuse_it() {
    FakeStore store;
    uint8_t a[32], b[32];
    TEST_ASSERT_EQUAL(KeyStatus::Created, load_or_create_seed(store, counter_random, a));
    TEST_ASSERT_EQUAL(1, store.saves);
    TEST_ASSERT_EQUAL(KeyStatus::Loaded, load_or_create_seed(store, counter_random, b));  // after a reset
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
    TEST_ASSERT_EQUAL(1, store.saves);  // never rewritten
}

void test_two_boards_get_different_keys() {
    FakeStore one, two;
    uint8_t a[32], b[32], pa[32], pb[32];
    g_state = 1;
    TEST_ASSERT_EQUAL(KeyStatus::Created, load_or_create_seed(one, counter_random, a));
    g_state = 777;  // another chip's random source
    TEST_ASSERT_EQUAL(KeyStatus::Created, load_or_create_seed(two, counter_random, b));
    public_key_of(a, pa);
    public_key_of(b, pb);
    TEST_ASSERT_TRUE(std::memcmp(pa, pb, 32) != 0);
}

void test_a_corrupt_record_is_refused_not_replaced() {
    FakeStore store;
    store.corrupt = true;
    uint8_t s[32];
    std::memset(s, 0xff, 32);
    TEST_ASSERT_EQUAL(KeyStatus::Corrupt, load_or_create_seed(store, counter_random, s));
    TEST_ASSERT_EQUAL(0, store.saves);
    uint8_t zero[32] = {};
    TEST_ASSERT_EQUAL_MEMORY(zero, s, 32);  // nothing usable left in the buffer
    FakeStore zeros;  // a stored all-zero seed is not a key either
    zeros.has = true;
    TEST_ASSERT_EQUAL(KeyStatus::Corrupt, load_or_create_seed(zeros, counter_random, s));
}

void test_a_broken_random_source_never_becomes_a_key() {
    FakeStore store;
    uint8_t s[32];
    TEST_ASSERT_EQUAL(KeyStatus::BadRandom, load_or_create_seed(store, zero_random, s));
    TEST_ASSERT_EQUAL(KeyStatus::BadRandom, load_or_create_seed(store, stuck_random, s));
    TEST_ASSERT_EQUAL(0, store.saves);
}

void test_a_key_that_cannot_be_stored_is_not_used() {
    FakeStore store;
    store.fail_save = true;
    uint8_t s[32];
    TEST_ASSERT_EQUAL(KeyStatus::StoreFailed, load_or_create_seed(store, counter_random, s));
    uint8_t zero[32] = {};
    TEST_ASSERT_EQUAL_MEMORY(zero, s, 32);
}

void test_device_id_comes_from_the_mac_address() {
    const uint8_t mac[6] = {0x34, 0x85, 0x18, 0x0a, 0xbc, 0xde};
    char id[32];
    device_id_from_mac(mac, id, sizeof id);
    TEST_ASSERT_EQUAL_STRING("ecu-3485180abcde", id);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_boot_creates_and_stores_a_key_later_boots_reuse_it);
    RUN_TEST(test_two_boards_get_different_keys);
    RUN_TEST(test_a_corrupt_record_is_refused_not_replaced);
    RUN_TEST(test_a_broken_random_source_never_becomes_a_key);
    RUN_TEST(test_a_key_that_cannot_be_stored_is_not_used);
    RUN_TEST(test_device_id_comes_from_the_mac_address);
    return UNITY_END();
}
