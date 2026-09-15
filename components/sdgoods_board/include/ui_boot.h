#pragma once

/**
 * Play the SDGOODS boot animation, then create and show the home screen.
 *
 * This function blocks for about 2 seconds while the animation plays.
 * It turns on the backlight, creates the boot screen, runs the LVGL
 * timer loop, deletes the boot screen and finally loads the home screen.
 */
void ui_boot_show(void);
