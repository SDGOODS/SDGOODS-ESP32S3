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

/* 设备级触摸手势策略（全设备统一口径，启动器与所有 app 共用）
 *
 * ============================ 问题（LVGL 机制，源码为证） ============================
 * LVGL 在「按下期间每个输入周期都会重新做一次命中测试」，并且会把当前按下对象
 * （act_obj）换成手指此刻所在的新对象。触发条件（lv_indev.c:837-838）：
 *
 *     else if (proc->types.pointer.scroll_obj == NULL &&
 *              lv_obj_has_flag(indev_obj_act, LV_OBJ_FLAG_PRESS_LOCK) == false) {
 *         ... 重新命中测试，把 act_obj 换成新对象 ...
 *     }
 *
 * 一旦接管发生（lv_indev.c:863-888）：给旧对象发 PRESS_LOST、给新对象发 PRESSED，
 * 并把 scroll_sum / gesture_sum / vect **全部清零** —— 于是新对象眼里这是一次
 * **全新的按下**，它自己的位移计数从 0 开始。后果：一次「滑动」只要中途掠过某个
 * 按钮 / 图标再松手，那个控件就收到「按下即松开」→ 位移 ≈ 0 → 判成点按 → **误触**。
 *
 * ⚠️ 关键事实（LVGL 8.3 源码，别凭感觉）：
 *   - lv_obj.c:438 `if(parent) obj->flags |= LV_OBJ_FLAG_PRESS_LOCK;`
 *     ⇒ **凡是「有父对象」的控件，LVGL 默认就已经带上 PRESS_LOCK**。
 *     ⇒ 真正缺它的，只有**无父对象的屏**，即 `lv_obj_create(NULL)` 建出来的 screen。
 *   - 所以「按下落在屏上、滑动时掠过某个图标/按钮」是唯一会接管成功的路径。
 *     本工程实测坐实过这一条：主页从空白处（= 屏本身）上滑滑到图标上，
 *     日志 `slot 0: tap (max move 4 px of 24) -> launch` ⇒ 图标被接管后位移只有
 *     4px，于是被当成点按、直接启动了 app。
 *   - 控制中心的「底部上滑却误触到按钮」是同一类症状：按下对象并非用户意图的对象
 *     （起手点落在按钮上、或落在捕获带之外的空白处），松手时 CLICKED 发给了按钮。
 *
 * =========================== 解法（三条，缺一不可） ===========================
 *   1) **所有权锁定**：给屏等「缺 PRESS_LOCK 的对象」补上 PRESS_LOCK
 *      ⇒ 手势在哪个对象上按下就归谁，绝不被中途接管。
 *      注：普通控件 LVGL 已默认带（见上），这里补的是屏。
 *   2) **点按位移守卫**：真正的动作只走 sdgoods_tap_bind()。它用 LV_EVENT_PRESSING
 *      逐周期跟踪「按下期间的最大位移」，只有 ≤ SDGOODS_TAP_SLOP 才触发
 *      ⇒ 按在按钮上滑走再松手不会触发按钮。
 *   3) **装饰物穿透**：lv_canvas 默认可点击（lv_canvas.c 构造时不改 flags），
 *      压在按钮上会**吃掉**按钮的点击；统一清掉它的 CLICKABLE 让触摸穿透。
 *      （lv_label 构造时已自动清 CLICKABLE（lv_label.c:716），无需处理，这里只做兜底。）
 *      若某个 canvas 是有意交互的（例如画板），用 sdgoods_tap_keep_interactive() 排除。
 *
 * ⚠️ PRESS_LOCK 与滚动无关：它在整个 LVGL 里**只在 lv_indev.c:838 这一处**被读取，
 *    lv_indev_scroll.c 完全不引用 ⇒ 加它**不会**锁死列表 / 容器滚动。
 *
 * ================================ 用法（app 作者） ================================
 *   什么都不用做：`sdgoods_app_shell_init()` 内部已经调 sdgoods_tap_install()，
 *   所有 app 构建出来即自动具备设备级手势行为。
 *   只有两种例外需要手动介入：
 *     - 界面**构建完成后**若又动态新建了控件，或自己建了新屏并 lv_scr_load()，
 *       调一次 sdgoods_tap_normalize(该屏)（看门狗最迟 100ms 后也会补上）。
 *     - 要一个「可交互的 canvas」（画板 / 手写），先把 sdgoods_tap_keep_interactive()
 *       调在它身上，否则它会被当成装饰物穿透。
 */

#pragma once

#include "lvgl.h"

/* 点按允许的最大位移（px）。取值依据：CST816 端点抖动实测上限约 8~10px，
 * 取 2~3 倍余量；而有意滑动普遍 ≥60px，两者之间有很宽的判别空间。 */
#define SDGOODS_TAP_SLOP  24

/* 点按回调：只有确认是点按（位移在阈值内）才会被调用。 */
typedef void (*sdgoods_tap_cb_t)(void *user_data);

/* 每个绑定的上下文。由**调用方提供静态存储**（不动态分配 ⇒ 没有失败路径，
 * 也就不会出现「内存不足导致按钮彻底失效」）。同一个对象销毁前不要复用同一份。 */
typedef struct {
    lv_point_t       start;      /* 按下起点 */
    int32_t          max_disp;   /* 按下期间最大位移（|dx|、|dy| 取大） */
    bool             valid;      /* 起点是否有效（拿不到 indev 时保持 false ⇒ 放行） */
    sdgoods_tap_cb_t cb;
    void            *ud;
} sdgoods_tap_ctx_t;

/* ★ 一行安装设备级手势策略（幂等，重复调用安全）：
 *   ① 立即对当前屏落实策略；
 *   ② 起一个 100ms 的看门狗，发现「当前屏换了」就对新屏再落实一次。
 *      为什么用看门狗而不是事件挂点：LVGL 8.3 没有「对象被创建」的公开钩子，
 *      这个版本的 lv_disp_t 也没有 disp 级事件回调（lv_disp_add_event_cb 不存在）；
 *      而唯一会缺 PRESS_LOCK 的只有无父屏，所以只要盯住 lv_scr_act() 的身份即可。
 *      开销：每个 tick 一次指针比较，仅在换屏时才做一次全树遍历。
 * 由 sdgoods_app_shell_init() 自动调用，app 一般不必自己调。 */
void sdgoods_tap_install(void);

/* 对整棵对象树落实统一手势策略（界面构建完成后调用一次）：
 *   - lv_label / lv_canvas：清 CLICKABLE（纯装饰，触摸穿透）
 *   - 其余可点击对象：加 PRESS_LOCK（按下期间锁定所有权，杜绝中途接管）
 * 可重复调用（幂等）。 */
void sdgoods_tap_normalize(lv_obj_t *root);

/* 单独给一个可交互对象加「按下锁定」（一般由 normalize 批量完成）。 */
void sdgoods_tap_lock(lv_obj_t *obj);

/* 把对象设为「触摸穿透」（纯装饰文字 / 指示条用）。 */
void sdgoods_tap_transparent(lv_obj_t *obj);

/* 声明「这个对象是有意交互的」：normalize 不再把它当装饰物去穿透，
 * 并给它补上 PRESS_LOCK。主要给「把 canvas 当画板用」的场景。 */
void sdgoods_tap_keep_interactive(lv_obj_t *obj);

/* 绑定「点按」动作：内部挂 PRESSED / PRESSING / CLICKED，逐周期跟踪按下期间
 * 的最大位移，只有 ≤ SDGOODS_TAP_SLOP 才调用 on_tap。内部自动加 PRESS_LOCK。
 * ctx 由调用方提供（建议 static），生命周期需覆盖该对象。
 * ⚠️ 不要再用 lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, ...) 直接绑动作，
 *    否则会绕过位移守卫（按下滑走再松手也会触发）。 */
void sdgoods_tap_bind(lv_obj_t *obj, sdgoods_tap_ctx_t *ctx,
                      sdgoods_tap_cb_t on_tap, void *user_data);

/* ============================ 无人手验证：合成触摸 ============================
 * 为什么平台层要有这个：手势行为**无法靠肉眼远程观察**（板子在别人手里 / 没有手指），
 * 而「点按 vs 滑动」判别恰恰是最容易写错、也最难回归的一类逻辑（本工程历史上错过两次）。
 * 唯一可靠的办法是让**固件自己跑一遍**，再用日志断言它走了哪个分支。
 *
 * 机制：临时把触摸 indev 的 read_cb 换成合成器，按帧产生「按下 → 逐帧移动 → 松开」，
 * 序列跑完的那一帧立刻把原 read_cb 还原并透传（绝不让真实触摸停留在被替换状态）。
 * 对 LVGL 而言这与真手指**完全等价**，因此串口日志里能直接看到判别结果
 * （例如 `slot 0: swipe 50 px ... -> not a tap, ignore` 或 `... -> launch`）。
 *
 * 平台层 weak 默认的 sdgoods_console_ext_cmd()（sdgoods_console.c）已把串口 **'1'..'5'**
 * 接到本接口上（见下面「平台内置自检按键」），所以**任何 app 都无需自己写调试代码**
 * 就能验证手势；启动器覆盖了 ext_cmd，按键语义保持它自己那套（含 A..H / j / k / 6..9）。
 *
 * 平台内置自检按键（app 未覆盖 ext_cmd 时生效）：
 *   '0' 点按 下排按钮位 (180,218)   → 控制中心第 5 个按钮（Power | Exit）
 *   '1' 点按 屏中央 (180,180)                → 验证按钮命中 / 点按
 *   '2' 顶部 (180,40) 下划 75px               → 打开控制中心（app 外壳手势）
 *   '3' 底部 (180,320) 上划 75px              → 返回主页 / 关闭浮层
 *   '4' 左缘 (20,180) 右滑 75px               → 返回上级
 *   '5' 点按后滑走（(70,157) 上滑 75px）      → 验证「滑动掠过不算点按」
 *
 * ⚠️ 仅用于调试；不要在正常交互路径里调用。 */

/* 合成一次触摸序列（可从**任意任务**调用，内部自己投递 lv_async_call）。
 * x,y   = 起手点（屏幕坐标）
 * dx,dy = 每帧位移（像素）；frames = 帧数（第 0..frames-2 帧按着，最后一帧松开）
 * 例：sdgoods_tap_synth(180, 40, 0, 25, 3) ⇒ 从顶部下划 75px，即「顶部下滑」。 */
void sdgoods_tap_synth(int x, int y, int dx, int dy, int frames);

/* 同 sdgoods_tap_synth，但**必须已在 LVGL 线程**中调用（直接改 indev 驱动状态）。
 * 适合「需要先把界面调整到位（如把列表滚回起点）再合成」的场景。 */
void sdgoods_tap_synth_now(int x, int y, int dx, int dy, int frames);

/* ---- 合成长按（长按类交互的离线核验手段，2026-09-19 新增） --------------------
 *
 * 为什么不能用 sdgoods_tap_synth(x, y, 0, 0, N) 代替：那条路径按**帧数**计时，
 * 而 LVGL 的长按阈值是**毫秒**（`driver->long_press_time`，由 `lv_indev_drv_init()`
 * 写入 `LV_INDEV_DEF_LONG_PRESS_TIME` = **400ms**，本工程未改过；indev 读周期
 * `LV_INDEV_DEF_READ_PERIOD` = 30ms）。帧数↔毫秒的换算依赖 read_period，
 * 一旦改配置就静默失效，所以这里直接按真实时间计时。
 *
 * ⚠️ **关键陷阱（读 lv_indev.c 的结论，别凭感觉）**：长按松手时 LVGL **仍会补发
 *    `LV_EVENT_CLICKED`** —— `lv_indev.c:975` 只把 `SHORT_CLICKED` 夹在
 *    `if(long_pr_sent == 0)` 里，而 `CLICKED`（:980）**不受它约束**。
 *    ⇒ 长按处理过的对象必须自己吃掉随后那一发 CLICKED，否则「长按弹菜单」会顺带
 *      触发「点按进 app」。调用方见 main/ui_launcher.c 的 s_app_long_pressed。
 *
 * hold_ms 默认 800（= 阈值的 2 倍）：给合成器的计时分辨率留余量。
 * ⚠️ 仅用于调试；不要在正常交互路径里调用。 */
void sdgoods_tap_synth_hold(int x, int y, int hold_ms);

/* 同 sdgoods_tap_synth_hold，但**必须已在 LVGL 线程**中调用。 */
void sdgoods_tap_synth_hold_now(int x, int y, int hold_ms);
