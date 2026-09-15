#pragma once

#include <stddef.h>

#define UI_SCAN_NAME_MAX  32

typedef int (*ui_scan_fn_t)(char names[][UI_SCAN_NAME_MAX + 1], size_t max_n);

void ui_scan_page_show(const char *title, const char *sub_fmt, ui_scan_fn_t scan_fn);
void ui_scan_page_poll(void);
