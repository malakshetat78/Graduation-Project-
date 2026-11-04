#ifndef ICVSP_TRANSPORT_H
#define ICVSP_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define REPORT_MAX 2048
#define REPORT_QUEUE 8
#define FRAME_MAX 244
#define ACK_TIMEOUT_MS 2000
#define MAX_ATTEMPTS 3
/* Pure state machine: all calls must be serialized by its owner. */
typedef struct { uint16_t id, len; uint8_t data[REPORT_MAX]; } report_t;
typedef struct {
    report_t items[REPORT_QUEUE];
    unsigned count, pos, attempts;
    uint32_t next_id, overflow_count;
    uint16_t room;
    bool up, waiting, stalled;
    uint64_t deadline;
} transport_t;
void transport_init(transport_t *t);
bool transport_enqueue(transport_t *t, const uint8_t *data, size_t len);
void transport_link(transport_t *t, bool up, uint16_t mtu);
size_t transport_frame(transport_t *t, uint64_t now, uint8_t *out);
void transport_sent(transport_t *t, uint64_t now);
bool transport_ack(transport_t *t, uint16_t id);
#endif
