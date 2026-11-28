// T-014 GNSS bring-up firmware: runs the L76K test (components/icvsp_gnss) on the pins in board_pins.h.
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gnss_test.h"

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the monitor after a reset
    run_gnss_test(GnssPins{GPS_UART, GPS_TX_GPIO, GPS_RX_GPIO, GPS_BAUD});
}
