#ifndef ICVSP_REPLAY_IDENTITY_H
#define ICVSP_REPLAY_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/* A different, strictly increasing boot_id is durably reserved BEFORE
 * signing any report from that boot. Each report gets a unique sequence.
 * Caller must stop issuing reports if these functions return false.
 * The 32-bit fields match the existing ICVSP schema-v2 backend.
 */
typedef struct {
    uint32_t boot_id;
    uint32_t next_sequence;
    bool active;
} icvsp_replay_identity_t;

/* Callback returns true ONLY after an atomic/durable update is committed.
 * Read: found=false means verified empty state, not I/O failure.
 */
typedef bool (*icvsp_boot_read_fn)(void *context, uint32_t *last_boot, bool *found);
typedef bool (*icvsp_boot_write_fn)(void *context, uint32_t new_boot);

bool icvsp_replay_begin(icvsp_replay_identity_t *state,
                        icvsp_boot_read_fn read_last,
                        icvsp_boot_write_fn commit_new,
                        void *context);

/* Never return a duplicated pair. On exhaustion, stop signing this boot. */
bool icvsp_replay_next(icvsp_replay_identity_t *state,
                       uint32_t *boot_id, uint32_t *sequence);

/* ESP-IDF secure storage adapter; disabled by default and FAILS CLOSED.
 * Not a strong hardware monotonic counter: full flash rollback/cloning
 * protection requires an additional hardware/server anti-rollback design.
 */
bool icvsp_replay_begin_on_device(icvsp_replay_identity_t *state);

#endif
