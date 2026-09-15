#pragma once

/* DEMO 入口页：圆形按钮启动器，承载 录音 / WIFI / 蓝牙 / 其他 四个演示功能。
 * 进入其中任一子页时，该子页关闭会通过 ui_nav_back() 回到本页（而非主页）。 */

void ui_demo_page_show(void);
void ui_demo_page_poll(void);
