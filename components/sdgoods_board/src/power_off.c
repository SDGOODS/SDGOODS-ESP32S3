#include "power_off.h"

#include "board_pins.h"
#include "st77916.h"

#include "driver/gpio.h"
#include "esp_sleep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void system_power_off(void)
{
    /* Turn the display off first so we don't leave a lit panel behind. */
    Set_Backlight(0);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Release the self-holding battery latch -> hard power cut.
       BOARD_BAT_CONTROL_LATCH_LEVEL is the level that keeps power on, so the
       opposite level releases it. */
    gpio_set_level(BOARD_BAT_CONTROL_GPIO,
                   BOARD_BAT_CONTROL_LATCH_LEVEL ? 0 : 1);

    /* Fallback: if power was NOT actually cut (e.g. the physical power button
       is still held and overrides the latch), drop into deep sleep and wake on
       the physical key so the badge at least stops consuming power. */
    esp_sleep_enable_ext0_wakeup(BOARD_KEY_GPIO, BOARD_KEY_ACTIVE_LEVEL);
    esp_deep_sleep_start();
}
