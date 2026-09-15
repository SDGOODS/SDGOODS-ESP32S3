#pragma once

#include <stddef.h>
#include "esp_err.h"

#define WIFI_SCAN_NAME_MAX  32
#define WIFI_SCAN_LIST_N    3

esp_err_t wifi_scan_init(void);
int wifi_scan_list(char names[][WIFI_SCAN_NAME_MAX + 1], size_t max_n);
