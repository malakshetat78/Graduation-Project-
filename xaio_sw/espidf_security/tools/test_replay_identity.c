/* Native unit tests for the same pure C replay_identity.c used by ESP-IDF. */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "replay_identity.h"

typedef struct {
    uint32_t saved;
    bool exists;
    bool read_ok;
    bool write_ok;
    unsigned int writes;
} MemoryStorage;

static bool read_last(void *ctx, uint32_t *v, bool *found)
{
    MemoryStorage *s = (MemoryStorage *)ctx;
    if (!s->read_ok) return false;
    *found = s->exists;
    *v = s->saved;
    return true;
}

static bool write_last(void *ctx, uint32_t v)
{
    MemoryStorage *s = (MemoryStorage *)ctx;
    ++s->writes;
    if (!s->write_ok) return false;
    s->saved = v;
    s->exists = true;
    return true;
}

int main(void)
{
    MemoryStorage disk = { .read_ok = true, .write_ok = true };
    icvsp_replay_identity_t first;
    uint32_t boot, seq;

    assert(icvsp_replay_begin(&first, read_last, write_last, &disk));
    assert(disk.saved == 1 && disk.writes == 1);
    assert(icvsp_replay_next(&first, &boot, &seq) && boot == 1 && seq == 1);
    assert(icvsp_replay_next(&first, &boot, &seq) && boot == 1 && seq == 2);
    puts("PASS first boot monotonic sequence");

    /* Simulate reboot: persistent boot counter stays, RAM sequence resets. */
    icvsp_replay_identity_t rebooted;
    assert(icvsp_replay_begin(&rebooted, read_last, write_last, &disk));
    assert(disk.saved == 2 && disk.writes == 2);
    assert(icvsp_replay_next(&rebooted, &boot, &seq) && boot == 2 && seq == 1);
    puts("PASS restart gives new boot ID");

    disk.write_ok = false;
    icvsp_replay_identity_t failed_commit;
    assert(!icvsp_replay_begin(&failed_commit, read_last, write_last, &disk));
    assert(!icvsp_replay_next(&failed_commit, &boot, &seq));
    assert(disk.saved == 2);
    puts("PASS interrupted/failed commit refuses signing");

    disk.write_ok = true;
    disk.read_ok = false;
    assert(!icvsp_replay_begin(&failed_commit, read_last, write_last, &disk));
    puts("PASS read failure refuses signing");

    disk.read_ok = true;
    disk.saved = UINT32_MAX;
    assert(!icvsp_replay_begin(&failed_commit, read_last, write_last, &disk));
    disk.saved = 0;
    assert(!icvsp_replay_begin(&failed_commit, read_last, write_last, &disk));
    puts("PASS invalid and exhausted boot ID rejected");

    icvsp_replay_identity_t sequence_overflow = {
        .boot_id = 4, .next_sequence = UINT32_MAX, .active = true
    };
    assert(icvsp_replay_next(&sequence_overflow, &boot, &seq));
    assert(boot == 4 && seq == UINT32_MAX);
    assert(!icvsp_replay_next(&sequence_overflow, &boot, &seq));
    puts("PASS sequence exhaustion stops signing");

    puts("PASS all persistent replay identity native tests");
    return 0;
}
