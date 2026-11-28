#pragma once
// Minimal stand-in for the Unity test framework, so the NMEA tests build with a plain C++ compiler
// (no PlatformIO). Only the checks the NMEA test (../device_logic/test/test_nmea) uses.
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace unity_min {
inline int failures = 0, tests = 0;
inline const char* current = "";
inline void fail(const char* file, int line, const char* what) {
    ++failures;
    std::printf("FAIL  %s  (%s:%d) %s\n", current, file, line, what);
}
}  // namespace unity_min

void setUp();
void tearDown();

#define UNITY_BEGIN() (unity_min::failures = 0, unity_min::tests = 0)
#define UNITY_END() \
    (std::printf("%d tests, %d failures: %s\n", unity_min::tests, unity_min::failures, unity_min::failures ? "FAIL" : "ALL PASS"), \
     unity_min::failures ? 1 : 0)
#define RUN_TEST(fn) do { unity_min::current = #fn; ++unity_min::tests; const int before = unity_min::failures; \
    setUp(); fn(); tearDown(); if (unity_min::failures == before) std::printf("PASS  %s\n", #fn); } while (0)
#define UMIN_CHECK(cond, what) do { if (!(cond)) unity_min::fail(__FILE__, __LINE__, what); } while (0)
#define TEST_ASSERT_TRUE(c) UMIN_CHECK((c), #c)
#define TEST_ASSERT_FALSE(c) UMIN_CHECK(!(c), "not " #c)
#define TEST_ASSERT_FALSE_MESSAGE(c, msg) UMIN_CHECK(!(c), msg)
#define TEST_ASSERT_EQUAL_INT64(e, a) UMIN_CHECK((int64_t)(e) == (int64_t)(a), #a " == " #e)
#define TEST_ASSERT_EQUAL_UINT32(e, a) UMIN_CHECK((uint32_t)(e) == (uint32_t)(a), #a " == " #e)
#define TEST_ASSERT_EQUAL_UINT8(e, a) UMIN_CHECK((uint8_t)(e) == (uint8_t)(a), #a " == " #e)
#define TEST_ASSERT_EQUAL_FLOAT(e, a) UMIN_CHECK(std::fabs((double)(e) - (double)(a)) <= 1e-5 * std::fmax(1.0, std::fabs((double)(e))), #a " == " #e)
#define TEST_ASSERT_FLOAT_WITHIN(d, e, a) UMIN_CHECK(std::fabs((double)(e) - (double)(a)) <= (double)(d), #a " within " #d " of " #e)
#define TEST_ASSERT_DOUBLE_WITHIN(d, e, a) UMIN_CHECK(std::fabs((double)(e) - (double)(a)) <= (double)(d), #a " within " #d " of " #e)
