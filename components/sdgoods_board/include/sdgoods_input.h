/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "lvgl.h"

esp_err_t sdgoods_touch_init(void);
void sdgoods_key_init(void);
void sdgoods_power_key_poll(void);   /* 主循环轮询电源键，短按返回/关菜单，长按松手后关机 */
void sdgoods_power_key_short_action(void);   /* 电源键「短按」的统一动作（真实短按松手与串口调试键 'P' 共用，保证路径一致） */
bool sdgoods_power_key_long_hold(void);
lv_indev_t *sdgoods_touch_get_indev(void);  /* 返回已注册的指针 indev 句柄（备用） */
lv_indev_state_t sdgoods_touch_get_state(void);  /* 最近一次触摸读取的状态（RELEASED/PRESSED） */
void sdgoods_touch_get_point(lv_point_t *p);     /* 最近一次触摸读取的坐标 */
void sdgoods_touch_synth_report(lv_indev_state_t st, lv_coord_t x, lv_coord_t y);
/* 合成触摸（sdgoods_tap.c）同步更新上面的轮询缓存。为什么要暴露：轮询型 app
 * （如小鸟游戏 READY 界面的 ready_tap_poll）读的是这里缓存的真实触摸状态，
 * 不经过 LVGL indev —— 合成点按若不同步写缓存，对这类 app 完全不可见，
 * 串口调试键 '1'..'5' 的「任何 app 都能验证手势」承诺就落空了。 */

/* ============================ ⚠️ 写 flash 前必调 ============================
 * `sdgoods_touch_flash_window(ms)` / `_end()`：告诉触摸层「接下来这段时间别碰 I2C」。
 *
 * 🔴 不调会怎样（2026-10-07 / 2026-10-31 真机抓到两次）：
 *   `Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0)` + `rst:0xc`。
 *   addr2line 落在 `cst816_read -> i2c_master_cmd_begin -> i2c_hw_fsm_reset`。
 *
 *   机理：SPI flash 写（一行 `fprintf` 落到 FAT 上就可能触发擦除 + 磨损均衡 =
 *   几十毫秒的**整片关 cache + 关中断**）期间，触摸 I2C 的中断服务代码在 flash 里
 *   ⇒ 取指停摆 ⇒ 事务拖过读超时 ⇒ 进 IDF 的 `i2c_hw_fsm_reset()` / `i2c_master_clear_bus()`
 *   —— 后者是**不 yield 的忙等**（`I2C_CLR_BUS_TIMEOUT_MS = 50`）⇒ 连续几趟把中断关够
 *   `CONFIG_ESP_INT_WDT_TIMEOUT_MS`（300 ms）⇒ 中断看门狗 panic ⇒ 设备重启。
 *   实测写盘期间单次触摸读 35~78 ms（正常 ~200 µs），日志特征 `touch: slow read: ... us`。
 *
 * 用法（写盘方自己负责配对，插件层不知道谁要写盘）：
 *
 *     sdgoods_touch_flash_window(300);   // 预估要写多久（ms，内部只延不缩）
 *     ...写盘...
 *     sdgoods_touch_flash_window_end();  // 内部再留 40 ms 静默收尾
 *
 * ⚠️ 窗口内触摸层**一个 I2C 事务都不发**，并且会先补一次 RELEASED（正按着也被松手，
 *    免得 UI 卡在按下态）；写盘结束后的第一次真实触摸有 ~40 ms + 一个 LVGL 周期的延迟，
 *    这是拿"点按迟钝 40 ms"换"设备不重启"，值得。
 * ⚠️ 上限 3000 ms（`SDGOODS_TOUCH_FLASH_WIN_MAX_MS`）：万一漏调 `_end()`，触摸也不会
 *    被永久关死。写盘超过 3 s 的场合（几百 KB 的分片下载）**每片各调一次**。
 * ⚠️ 这只是"声明式"防护，漏调一处就漏一处；触摸层另有**慢读自适应退避**
 *    （连续读慢就自己拉长轮询间隔）兜底，两者缺一不可。 */
#define SDGOODS_TOUCH_FLASH_WIN_MAX_MS 3000
void sdgoods_touch_flash_window(int ms);
void sdgoods_touch_flash_window_end(void);
