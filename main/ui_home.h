#pragma once

#define UI_HOME_BTN_SIZE   76
#define UI_HOME_BTN1_X     48
#define UI_HOME_BTN1_Y     93
#define UI_HOME_BTN2_X     142
#define UI_HOME_BTN2_Y     93
#define UI_HOME_BTN3_X     236
#define UI_HOME_BTN3_Y     93
#define UI_HOME_BTN4_X     48
#define UI_HOME_BTN4_Y     191
#define UI_HOME_BTN5_X     142
#define UI_HOME_BTN5_Y     191
#define UI_HOME_BTN6_X     236
#define UI_HOME_BTN6_Y     191

#define UI_HOME_TITLE_Y    35
#define UI_HOME_FOOTER_Y   296

void ui_home_create(void);
void ui_home_show(void);

/* 子页返回目标：进入子页（如 DEMO 下的录音页）时由父页改写为自己的 show 函数，
 * 子页关闭时调用 ui_nav_back() 即可回到正确的父页（而不是固定跳回主页）。
 * 默认指向 ui_home_show（根页面）。 */
extern void (*ui_nav_parent_show)(void);
void ui_nav_back(void);
