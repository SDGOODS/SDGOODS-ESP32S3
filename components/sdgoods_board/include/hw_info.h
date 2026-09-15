#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t hw_info_init(void);

float hw_info_bat_v(void);

bool hw_info_gyro(int *x, int *y, int *z);

float hw_info_space_mb(void);
