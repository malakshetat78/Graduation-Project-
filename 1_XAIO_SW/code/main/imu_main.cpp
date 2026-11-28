// T-010 IMU bring-up firmware: runs the MPU6050 test (components/icvsp_imu) on the pins in board_pins.h.
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_test.h"

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the monitor after a reset
    run_imu_test(ImuPins{IMU_I2C_PORT, IMU_SDA_GPIO, IMU_SCL_GPIO, IMU_I2C_HZ});
}
