#include "replay_identity.h"
#include "secure_storage.h"
#include "sdkconfig.h"

#if CONFIG_ICVSP_ENABLE_SECURE_NVS
static bool read_previous(void *unused, uint32_t *previous, bool *found)
{
    (void)unused;
    return secure_storage_read_boot_counter(previous, found);
}

static bool persist_next(void *unused, uint32_t value)
{
    (void)unused;
    return secure_storage_write_boot_counter(value);
}
#endif

bool icvsp_replay_begin_on_device(icvsp_replay_identity_t *state)
{
    if (state == NULL) return false;
#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (!secure_storage_init()) return false;
    return icvsp_replay_begin(state, read_previous, persist_next, NULL);
#else
    /* RAM-only development keys are not stable device identities.
     * Do not issue persistent sequence numbers without encrypted storage. */
    state->boot_id = 0;
    state->next_sequence = 0;
    state->active = false;
    return false;
#endif
}
