#pragma once
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#define SERIAL_8N1 0
extern uint32_t fake_now;
inline uint32_t millis() { return fake_now; }
struct FakeSerial {
    std::deque<char> input;
    void begin(unsigned long, int, int, int) {}
    int available() { return static_cast<int>(input.size()); }
    int read() { if (input.empty()) return -1; char c=input.front(); input.pop_front(); return c; }
    void push(const std::string& s) { for(char c:s) input.push_back(c); }
};
extern FakeSerial Serial, Serial1;
struct String {
    std::string value;
    String(float f, int n) { char b[64]; std::snprintf(b,sizeof b,"%.*f",n,f); value=b; }
    const char* c_str() const { return value.c_str(); }
};
