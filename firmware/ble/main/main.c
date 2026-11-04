#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "icvsp_ble.h"
#include "sdkconfig.h"
void app_main(void) {
    /* Do not automatically erase NVS: it may hold identity material in a merged project. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(icvsp_ble_start());
#ifdef CONFIG_ICVSP_TRANSPORT_DEMO
    uint8_t demo[706];
    memset(demo,'D',sizeof(demo));
    const char *prefix="UNSIGNED-TRANSPORT-TEST:";
    memcpy(demo,prefix,strlen(prefix));
    ESP_ERROR_CHECK(icvsp_ble_enqueue(demo,sizeof(demo)));
    ESP_LOGW("ICVSP","Queued UNSIGNED 706-byte transport diagnostic; NOT a hazard report");
#endif
    /* Real integration, AFTER your existing signing routine:
     * ESP_ERROR_CHECK(icvsp_ble_enqueue(signed_wire_bytes, signed_wire_length));
     * Handle ESP_ERR_TIMEOUT in producer (retain and retry); do not drop silently.
     */
}
