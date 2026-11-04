#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static transport_t t;
static uint8_t payload[REPORT_MAX], out[FRAME_MAX], rebuilt[REPORT_MAX];
static void roundtrip(unsigned mtu, unsigned len) {
    transport_init(&t); assert(transport_enqueue(&t,payload,len));
    transport_link(&t,true,mtu);
    unsigned offset=0, parts=0;
    while(!t.waiting) {
        size_t n=transport_frame(&t,0,out); assert(n>4 && n<=FRAME_MAX);
        assert(n<=mtu-3); assert(out[2]==parts);
        memcpy(rebuilt+offset,out+4,n-4); offset+=n-4; ++parts;
        transport_sent(&t,0);
    }
    assert(offset==len && memcmp(payload,rebuilt,len)==0);
    assert(parts==out[3]); assert(!transport_ack(&t,99));
    assert(transport_ack(&t,1) && t.count==0);
}
int main(void) {
    for(unsigned i=0;i<REPORT_MAX;++i) payload[i]=(uint8_t)i;
    roundtrip(23,706); roundtrip(185,706); roundtrip(247,706);
    roundtrip(517,2048); roundtrip(23,2048); roundtrip(23,1);
    transport_init(&t); assert(!transport_enqueue(&t,payload,0));
    assert(!transport_enqueue(&t,payload,REPORT_MAX+1));
    assert(transport_enqueue(&t,payload,1));
    assert(transport_frame(&t,0,out)==0); // disconnected
    transport_link(&t,true,23);
    assert(!transport_ack(&t,1)); // premature ACK
    assert(transport_frame(&t,0,out)>0);
    assert(transport_frame(&t,0,out)>0 && t.pos==0); // notify failure doesn't advance
    transport_sent(&t,0);
    assert(transport_frame(&t,1999,out)==0);
    assert(transport_frame(&t,2000,out)>0); transport_sent(&t,2000);
    assert(transport_frame(&t,4000,out)>0); transport_sent(&t,4000);
    assert(transport_frame(&t,6000,out)==0 && t.stalled && t.count==1);
    assert(transport_ack(&t,1)); // late ACK remains useful
    transport_init(&t);
    for(unsigned i=0;i<REPORT_QUEUE+1;++i) assert(transport_enqueue(&t,payload,1));
    assert(t.count==REPORT_QUEUE && t.items[0].id==2 && t.overflow_count==1);
    transport_link(&t,true,247);
    transport_frame(&t,0,out); transport_sent(&t,0);
    transport_link(&t,false,23); assert(t.count==REPORT_QUEUE);
    transport_link(&t,true,23); assert(t.pos==0 && t.attempts==0);
    t.next_id=65536; assert(!transport_enqueue(&t,payload,1));
    puts("PASS: byte roundtrip/MTU, ACK, retries, stalls, disconnect, overflow, ID exhaustion");
}
