#pragma once

/*
 * sdgoods_hooks.h —— 应用层「注册」给平台层的少数几个回调
 *
 * 为什么要这么一层
 * ----------------
 * 平台层（components/sdgoods_board）负责屏 / 触摸 / 音频 / 应用框架，
 * 它**不知道主页长什么样、也不知道有哪些应用**。但有三处必须回调应用层：
 *
 *   1. LVGL 主循环每轮要推进各应用的定时器（精灵、物理、蓝牙收包）；
 *   2. 开机动画播完，要创建并进入首屏；
 *   3. 应用菜单点「退出」、或游戏中短按电源键，要回到主页 / 应用页。
 *
 * 早期写法是平台层直接 #include 应用的头文件（lvgl_port.c 引了 9 个 ui_*_poll），
 * 结果是「底板反过来依赖上层」：任何人改应用都得碰平台代码，没法单独替换。
 * 现在改成注册制 —— 平台层只认这几个函数指针。
 *
 * 装配点只有一个：main/apps/apps_registry.c，由 main.c 调用。
 * 没注册时全部退化为空操作，不会空指针崩溃。
 */

typedef void (*sdgoods_cb_t)(void);

/* ---- 1) 应用轮询 ----------------------------------------------------------
 * lvgl_port_loop() 每轮（约 2ms）调用一次。各应用的 *_poll() 由应用层自己汇总。 */
void sdgoods_apps_set_poll(sdgoods_cb_t fn);

/* ---- 2) 屏幕导航 ---------------------------------------------------------- */
typedef struct {
    /* 开机动画结束：创建主页并显示（首次进入用，不得为 NULL） */
    sdgoods_cb_t home_create_show;
    /* 仅切到主页（主页已创建）。菜单「退出」/ 游戏中短按电源键回这里 */
    sdgoods_cb_t home_show;
    /* 切换回「应用」启动台页。菜单「退出」默认回这里 */
    sdgoods_cb_t apps_show;
} sdgoods_nav_t;

void sdgoods_ui_set_nav(const sdgoods_nav_t *nav);

/* ---- 以下为平台层内部使用（应用层不需要调用） ----------------------------- */
void sdgoods_apps_poll(void);            /* 由 lvgl_port_loop() 调用 */
void sdgoods_ui_home_create_show(void);  /* 由 ui_boot.c 在动画结束时调用 */
void sdgoods_ui_home_show(void);         /* 由 ui_app_shell.c 调用 */
void sdgoods_ui_apps_show(void);         /* 由 ui_app_shell.c 调用 */
