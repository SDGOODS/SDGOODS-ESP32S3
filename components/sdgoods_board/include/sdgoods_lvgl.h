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

#include <stdbool.h>

void sdgoods_lvgl_init(void);
void sdgoods_lvgl_loop(void);

/* ================= 跨任务 → LVGL 线程的投递原语（唯一正确入口） =================
 *
 * 【什么时候必须用它】
 * 只要**调用方不是 LVGL 线程**（console RX 任务、Wi-Fi / BLE 事件任务、
 * esp_timer 回调、任何 xTaskCreate 出来的任务…）而它又想碰 LVGL ——
 * 建/删对象、改界面、替换 indev 的 read_cb —— 必须用本函数把动作投递进 LVGL 线程。
 *
 * 【为什么不能用 lv_async_call 代替】（2026-09-19 真机定位，别再踩）
 *   `lv_async_call()` 内部是 `lv_mem_alloc()` + `lv_timer_create()`，后者的
 *   `_lv_ll_ins_head()` **直接改 LVGL 那条全局、无锁的定时器链表**；而 LVGL 线程
 *   同一时刻可能正在 `lv_timer_handler()` 里遍历 / 摘除同一张链表
 *   （`_lv_ll_remove` + `lv_mem_free`）。两个任务同时改一条链表的头与邻接指针，
 *   链表就会指向已释放内存 ⇒ `lv_timer_exec()` 取到垃圾回调 ⇒ 硬错误
 *   `InstructionFetchError`，且 **PC 落在 PSRAM（0x3c…）而不是代码段**。
 *   实测复现率：「槽清单变化事件 + 弹槽满浮层」这条路 6~8 轮内必崩；
 *   改用本函数后连跑 12 轮全稳（同一条 UI 代码）。
 *
 * 【语义】
 *   · 可在任意任务调用；在 LVGL 线程里调用也合法（等价于「下一拍执行」）。
 *   · 回调在 LVGL 线程、且是在 `lv_timer_handler()` **之前**消费的，不在任何 LVGL
 *     内部结构的临界区里 ⇒ 回调里可以自由建 / 删整棵对象树、替换 indev 驱动状态。
 *   · 队列深度 8。满时**丢弃新请求**并打一条 ESP_LOGW：不阻塞调用方，也不打乱
 *     已经排好序的老请求（调试通道需要「顺序」多于需要「不丢」）。
 *   · 全程零内存分配（定长数组 + 临界区），唯一的失败路径就是「队列满」。
 *   · ⚠️ 回调里不要无条件再次 post 自己：drain 会把同一批里新投递的也执行掉，
 *     自投递会喂成死循环（已加每轮上限兜底，但那是护栏不是许可）。
 *
 * @param cb   要在 LVGL 线程执行的回调（必须非空）
 * @param arg  透传给 cb 的参数（本函数不接管其生命周期）
 * @return     入队成功 true；队列满被丢弃 false
 *
 * 例：
 *     static void on_ready(void *arg) { lv_label_set_text((lv_obj_t *)arg, "ok"); }
 *     sdgoods_lvgl_post(on_ready, label);     // 从任意任务调用
 */
typedef void (*sdgoods_lvgl_cb_t)(void *arg);
bool sdgoods_lvgl_post(sdgoods_lvgl_cb_t cb, void *arg);
