#include <Arduino.h>
#include "log.h"
#include <cassert>
#include <cstdarg>
#include <iostream>
uint32_t fake_now = 100;
FakeSerial Serial, Serial1;
std::string logs;
int log_line(const char* fmt, ...) {
    char b[1024]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(b,sizeof b,fmt,ap); va_end(ap); logs+=b; return n;
}
void step(int,int,const char*) {}
#include "gnss_app.cpp"
std::string sentence(const std::string& body) {
    unsigned sum=0; for(unsigned char c:body) sum^=c;
    char end[8]; snprintf(end,sizeof end,"*%02X\r\n",sum);
    return "$"+body+end;
}
const std::string rmc=sentence("GNRMC,123519,A,4807.038,N,01131.000,E,0,0,231126,,");
const std::string gga=sentence("GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,0,M,0,M,,");
void tick(uint32_t t,const std::string& data="") { fake_now=t; Serial1.push(data); gnss_app_loop(); }
void reset(uint32_t t=100) {
    parser=icvsp::NmeaParser{}; start_ms=t; last_byte_ms=last_valid_nmea_ms=last_print_ms=0;
    bytes_seen=last_good_count=0; got_any_byte=got_valid_nmea=false; last_status=Status::STARTING;
    Serial.input.clear(); Serial1.input.clear(); logs.clear(); fake_now=t;
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    test=Test::NONE; recovering=saw_expected=wait_for_sentence=false;
#endif
}
int main() {
    reset(); tick(100); assert(last_status==Status::WAITING_UART);
    tick(5099); assert(last_status==Status::WAITING_UART);
    tick(5100); assert(last_status==Status::DISCONNECTED);
    reset(); tick(100,rmc+gga); assert(last_status==Status::FIX);
    tick(2600); assert(parser.fix(2600).valid);
    tick(2601); assert(last_status==Status::STALE); assert(!parser.fix(2601).valid);
    assert(parser.fix(2601).lat==0 && parser.fix(2601).lon==0);
    tick(3600); assert(last_status==Status::STALE);
    tick(3601); assert(last_status==Status::DISCONNECTED);
    tick(4000,rmc); assert(last_status==Status::STALE);
    tick(4001,gga); assert(last_status==Status::FIX);
    tick(4100,sentence("GNRMC,123520,V,,,,,0,0,231126,,")+gga);
    assert(last_status==Status::NO_FIX);
    tick(4200,rmc+gga); assert(last_status==Status::FIX);
    tick(6800,"$GNRMC,bad*00\r\n"); assert(last_status==Status::STALE);
    reset(0xffffff00u); tick(0xffffff00u,rmc+gga);
    tick(uint32_t(0xffffff00u+2501u)); assert(last_status==Status::STALE);
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    for(char command:std::string("SD")) {
        reset(); tick(100,rmc+gga); Serial.push(std::string(1,command)); tick(200);
        for(uint32_t t=1200;t<=5200;t+=1000) tick(t,rmc+gga);
        assert(last_status==(command=='S'?Status::STALE:Status::DISCONNECTED));
        assert(!parser.fix(5200).valid);
        Serial.push("S"); tick(5300); assert(logs.find("BUSY")!=std::string::npos);
        tick(6200,"ignored\r\n"); assert(last_status!=Status::FIX);
        tick(6201,rmc); assert(last_status!=Status::FIX);
        tick(6202,gga); assert(last_status==Status::FIX);
        assert(test==Test::NONE);
        assert(logs.find(std::string("GNSS TEST ")+command+" PASS")!=std::string::npos);
        assert(logs.find("FAIL")==std::string::npos);
        std::cout<<"PASS "<<command<<" timeout and fresh-pair recovery\n";
    }
    // Injection duration must also survive the 32-bit millis() rollover.
    reset(0xfffff000u); tick(0xfffff000u,rmc+gga);
    Serial.push("D"); tick(0xfffff100u);
    tick(uint32_t(0xfffff100u+4000u)); assert(last_status==Status::DISCONNECTED);
    tick(uint32_t(0xfffff100u+6000u),rmc+gga);
    assert(test==Test::NONE && logs.find("GNSS TEST D PASS")!=std::string::npos);
    // A tail from a sentence interrupted by injection cannot complete an old fix.
    reset(); tick(100,rmc+gga); tick(150,rmc.substr(0,20));
    Serial.push("S"); tick(200);
    tick(3000,"noise"); tick(4000,"noise"); tick(6200,rmc.substr(20));
    assert(last_status!=Status::FIX);
    tick(6201,gga); assert(last_status!=Status::FIX);
    tick(6202,rmc); assert(last_status==Status::FIX && test==Test::NONE);
    reset(); Serial.push("S"); tick(100); assert(test==Test::NONE);
    assert(logs.find("SKIP")!=std::string::npos);
    reset(); tick(100,rmc+gga); Serial.push("D"); tick(200);
    tick(4000); tick(6200); tick(21200);
    assert(test==Test::NONE && logs.find("FAIL: no valid recovery")!=std::string::npos);
#else
    reset(); tick(100,rmc+gga); Serial.push("SD"); tick(200);
    assert(Serial.available()==2); assert(logs.find("GNSS TEST")==std::string::npos);
#endif
    std::cout<<"PASS startup, freshness boundaries, disconnect, recovery, NO_FIX, bad checksum, clock wrap\n";
}
