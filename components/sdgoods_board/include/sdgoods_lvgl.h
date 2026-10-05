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

/* ================= UI 活动闸门（跨任务只读） =================
 *
 * 「UI 现在需不需要独占流畅」的**唯一真相源**，由 LVGL 主循环每拍（≈10 ms）刷新，
 * 任意任务可读（volatile bool，无需锁）。
 *
 * 【为什么需要】写 flash 时 SPI1 与 cache 互斥 ⇒ 全芯片「关 cache + 关中断」，
 * LVGL 的渲染代码全在 flash 上 ⇒ 渲染帧被切断（= 用户看到的滑动/弹窗卡顿）。
 * 这个冻结**无法消除**（本机颗粒无 suspend 驱动，见 sdgoods_lvgl.c 的注释），
 * 只能**挑时机**：UI 空闲时才写，UI 活动/动画时让路。
 *
 * 【判据（任一成立即为 busy）】
 *   · 最近 LVGL_UI_IDLE_MS 内有过一次 flush —— UI 刚重绘过（弹窗/换页/列表刷新）；
 *   · 还有动画在跑 —— 滑动惯性/回弹、弹窗动效（LVGL 的 scroll 动量就是 lv_anim）；
 *   · 手指按着 —— 正在拖拽/点按，必须跟手。
 * 熄屏（suspended）恒为 false：那时不渲染，没有要保护的东西。
 *
 * 【消费者】当前**这一档没有消费者**。原先唯一的消费者是 `sdgoods_devlink.c` 的写盘
 * 让路闸门，2026-10-02 第六轮已改用只看触摸的 `sdgoods_ui_interactive()`（下载进度环
 * 是自发动画 ⇒ busy 恒真 ⇒ 不碰屏也在让路，下载被压到 17 KB/s）。
 * 保留这一档是因为「对自发动画也让路」在别的策略里可能仍有用（例如将来做「下载期
 * 暂停一切动画」），但**别不加思索地把它塞回写盘闸门**。 */
bool sdgoods_ui_busy(void);

/* ================= 用户交互闸门（比 ui_busy 严一档） =================
 *
 * 【与 sdgoods_ui_busy() 的区别 —— 关键，别混用】
 *   · `ui_busy` = 「UI 在重绘」：动画在跑 / 刚 flush 过 / 手指按着。**下载进度环
 *     是自发动画**，一出现就让它恒为 true。
 *   · `ui_interactive` = 「**用户**在操作」：只看触摸（按下中 ‖ 最近
 *     LVGL_UI_TOUCH_HOLD_MS 内按下过）。进度环自转不算。
 *
 * 【为什么需要这一档】预擦（install_begin 的整段擦除）是**一次性、不可分割**的大活：
 * 2 MB 要 4 s+，期间 LVGL 一帧都画不出来。用 `ui_busy` 当让路判据会让它被进度环的
 * 自发动画永久挡住（实测让路占空比 >67%，4 s 直接拖成 13 s+）。而**用户可感知的卡顿
 * 只来自「操作了但屏幕没反应」** —— 所以预擦只对 `ui_interactive` 让路：
 * 用户不动手 ⇒ 擦除全速跑完；用户一动 ⇒ 立刻让开让他画完这一帧。
 *
 * 【残留风险（已接受）】松手后的滚动惯性持续时长若超过 HOLD 窗，惯性尾声会被冻结
 * 切断一下。取 400 ms 是覆盖弹窗动效（~200 ms）与 LVGL 滚动动量主段的折中。
 *
 * 消费者（两个）：
 *   · `slot_manifest.c` 的预擦分块循环（`install_erase_upto` / 泵擦）；
 *   · `sdgoods_devlink.c` 的写盘让路闸门（`dl_sinkq_ui_gate`，2026-10-02 第六轮换过来的）。
 * ⚠️ 两处的护栏**不同**，别互相照抄：预擦是「用户一动就整段停下」（靠下载期并行掩盖
 *    总时长变长）；写盘闸门带令牌桶（≤67 % 占空比 + 呼吸闸），所以**持续触摸时下载
 *    仍能保住约 1/3 原速**，不会把 TCP 饿到反压超时。 */
bool sdgoods_ui_interactive(void);

/* 第 4 档「动画推进周期提速」的运行时开关（2026-10-03）。
 * 默认开：交互期把 lv_anim 的**驱动定时器**周期 30ms → 16ms。
 * 🔴 它与"上屏刷新周期"是**两颗独立的 timer**：只提上屏的话，位移数值本身仍 30ms 才更新
 *    一次 ⇒ 180ms 的整屏位移只有 ~6 帧（每帧跳 60px），肉眼就是掉帧。
 * **只给 A/B 验收用**：关掉它 = 回到老行为，当反例对照，可证明"掉帧来自动画定时器周期"。
 * 串口入口 `{anim0!` / `{anim1!`（见 launcher_main.c 的注入分发）。
 * ⚠️ 只影响"多久推进一帧动画"，不改任何动画时长与布局 ⇒ 画面逐像素一致。 */
void sdgoods_lvgl_anim_boost_enable(bool en);
