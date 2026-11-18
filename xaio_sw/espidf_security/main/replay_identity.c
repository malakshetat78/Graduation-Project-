#include "replay_identity.h"

#include <limits.h>
#include <string.h>

bool icvsp_replay_begin(icvsp_replay_identity_t *state,
                        icvsp_boot_read_fn read_last,
                        icvsp_boot_write_fn commit_new,
                        void *context)
{
    if (state == NULL) return false;
    memset(state, 0, sizeof(*state));
    if (read_last == NULL || commit_new == NULL) return false;

    uint32_t previous = 0;
    bool found = false;
    if (!read_last(context, &previous, &found)) return false;
    if (found && (previous == 0 || previous == UINT32_MAX)) return false;

    /* Must commit before activating. A failed commit refuses signing. */
    const uint32_t reserved = found ? previous + 1U : 1U;
    if (!commit_new(context, reserved)) return false;

    state->boot_id = reserved;
    state->next_sequence = 1U;
    state->active = true;
    return true;
}

bool icvsp_replay_next(icvsp_replay_identity_t *state,
                       uint32_t *boot_id, uint32_t *sequence)
{
    if (state == NULL || !state->active || boot_id == NULL || sequence == NULL)
        return false;
    if (state->next_sequence == 0) {
        state->active = false;
        return false;
    }
    *boot_id = state->boot_id;
    *sequence = state->next_sequence;
    if (state->next_sequence == UINT32_MAX) {
        state->active = false;
    } else {
        ++state->next_sequence;
    }
    return true;
}
