#include "transport.h"
#include <string.h>
static void restart(transport_t *t) {
    t->pos=0; t->attempts=0; t->waiting=false; t->stalled=false;
}
static void pop(transport_t *t) {
    --t->count;
    memmove(t->items, t->items+1, t->count*sizeof(report_t));
    restart(t);
}
void transport_init(transport_t *t) {
    memset(t,0,sizeof(*t)); t->next_id=1; t->room=16;
}
bool transport_enqueue(transport_t *t, const uint8_t *data, size_t len) {
    if (!data || !len || len>REPORT_MAX || t->next_id>65535) return false;
    /* Never evict an unacknowledged report. Backpressure the producer. */
    if (t->count==REPORT_QUEUE) return false;
    report_t *r=&t->items[t->count++];
    r->id=(uint16_t)t->next_id++; r->len=(uint16_t)len;
    memcpy(r->data,data,len); return true;
}
void transport_link(transport_t *t, bool up, uint16_t mtu) {
    t->up=up && mtu>=23;
    /* Freeze fragment geometry per connection, cap notification to 244 bytes. */
    t->room=(mtu>=247 ? 244 : (mtu>=23 ? mtu-3 : 20))-4;
    restart(t);
}
size_t transport_frame(transport_t *t, uint64_t now, uint8_t *out) {
    if (!t->up || !t->count || t->stalled) return 0;
    if (t->waiting) {
        if (now<t->deadline) return 0;
        if (t->attempts>=MAX_ATTEMPTS) { t->stalled=true; return 0; }
        t->waiting=false; t->pos=0;
    }
    report_t *r=&t->items[0];
    unsigned total=(r->len+t->room-1)/t->room;
    unsigned offset=t->pos*t->room;
    size_t n=r->len-offset;
    if (n>t->room) n=t->room;
    out[0]=r->id>>8; out[1]=r->id&255;
    out[2]=(uint8_t)t->pos; out[3]=(uint8_t)total;
    memcpy(out+4,r->data+offset,n);
    return n+4;
}
void transport_sent(transport_t *t, uint64_t now) {
    if (!t->count || t->waiting || !t->up || t->stalled) return;
    if (t->pos==0) ++t->attempts;
    ++t->pos;
    if (t->pos*t->room>=t->items[0].len) {
        t->waiting=true; t->deadline=now+ACK_TIMEOUT_MS;
    }
}
bool transport_ack(transport_t *t, uint16_t id) {
    if (!t->up || !t->count || !t->waiting || t->items[0].id!=id) return false;
    pop(t); return true;
}
