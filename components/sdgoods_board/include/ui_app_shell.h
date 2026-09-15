#pragma once

#include "lvgl.h"

void ui_app_shell_init(void);
void ui_app_shell_bind(lv_obj_t *scr);
void ui_app_shell_set_exit_cb(void (*cb)(void));
void ui_app_shell_set_pause_cb(void (*cb)(void));
void ui_app_shell_set_resume_cb(void (*cb)(void));

void ui_app_shell_menu_open(void);
void ui_app_shell_menu_close(void);
bool ui_app_shell_menu_is_open(void);
bool ui_app_shell_is_app_active(void);

void ui_app_shell_leave(bool to_home);

void ui_app_volume_up(void);
void ui_app_volume_down(void);
int  ui_app_volume_get(void);
