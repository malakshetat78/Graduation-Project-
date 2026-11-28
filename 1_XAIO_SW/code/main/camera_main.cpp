// T-012 camera bring-up firmware: runs the camera test once (components/icvsp_camera), then blinks the user LED:
// slow (1 s) = every check passed, fast (0.2 s) = at least one FAIL line. Press RESET to run it again.
#include "board_pins.h"
#include "camera_test.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the monitor after a reset
    const int failures = run_camera_test();

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    const TickType_t half_period = pdMS_TO_TICKS(failures == 0 ? 1000 : 200);
    for (bool on = false;; on = !on) {
        gpio_set_level(LED_GPIO, on ? 0 : 1);  // active low
        vTaskDelay(half_period);
    }
}
