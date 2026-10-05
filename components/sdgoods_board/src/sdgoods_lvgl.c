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

#include "sdgoods_lvgl.h"

#include "sdgoods_lcd.h"
#include "sdgoods_input.h"
#include "sdgoods_power.h"
#include "lvgl.h"
#include "sdgoods_hooks.h"   /* sdgoods_apps_poll：应用层注册过来的轮询汇总 */

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
#include "esp_cache.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 绘制缓冲优先用【全屏 PSRAM 双块】(360*360*2 = 259200 字节/块)，两块轮流，异步 flush。
 *
 * ⚠️ 为什么必须是全屏（用户可见结论，勿再改回小块！）：
 * LVGL 的每一次 lv_disp flush = 一次 draw_bitmap = CASET/RASET/RAMWR + 若干 tx_color 分片。
 * 面板没有 VSYNC/TE 门控，屏上内容按 DMA 完成顺序「一条条」更新，任意瞬间屏上是【不同滚动
 * 偏移的若干横带】—— 用户原话「首页图标左右滑动，像断成 3 段分开」。块高越小越明显
 * （20 行块 => 整屏 18 次 flush，横滑时尤其刺眼）。
 * 改成全屏缓冲后：一次 flush 就覆盖整块无效区，连续推送无间隙 => 最多一道极细的推进线。
 * 带宽校验（这是成立的物理前提）：QSPI 80MHz×4bit ≈ 40MB/s，
 *   整屏 259200B ≈ 6.5ms  < 面板一轮扫描 16.7ms   ✅
 *   主页图标行 360×150=108000B ≈ 2.7ms < 该区扫描 ~7ms ✅
 * 即「推送快过面板扫描」，撕裂基本不可见。
 *
 * ⚠️ 与 PSRAM 缓冲强绑定的内存约束（历史踩坑，两条必须同时满足）：
 * PSRAM 不是 DMA-capable（esp_ptr_dma_capable 只认内部 SRAM 窗口），spi_master 的
 * setup_priv_desc 会为【每个在途事务】malloc 一块内部 DMA 弹跳缓冲，大小 = 分片字节数
 * （LCD_SPI_MAX_TRANSFER_SIZE），失败即 ESP_ERR_NO_MEM(0x101)，panel_io_spi 的分片循环
 * 随即中断 -> 画面残缺/黑带。
 *   ⇒ 弹跳预算 = LCD_SPI_TRANS_QUEUE_SZ × LCD_SPI_MAX_TRANSFER_SIZE 必须 < 内部 DMA 堆。
 *     实测内部 DMA 最大连续空闲块仅 31744B，故队列深度已在 sdgoods_lcd.h 降到 2（16KB）。
 *   ⇒ 只恢复 PSRAM 而保留队列=40 会立刻 0x101（这正是当初误判「PSRAM 是死穴」的真凶）。
 *
 * flush_cb 为「异步」：仅把数据交给 DMA 就返回，CPU 立刻渲染另一块；DMA 传完由
 * on_color_done 回调调 lv_disp_flush_ready(drv)，渲染与传输重叠 -> 流畅。
 *
 * 回退阶梯（PSRAM 不足时才走，视觉上会重新出现轻微横带）：
 *   ① 全屏 PSRAM 双块 + 异步（默认，1 flush/帧）
 *   ② 内部 SRAM 40 行双块 + 异步
 *   ③ 内部 SRAM 20 行单块 + 同步 flush（最末，稳但不快） */
#define LVGL_TICK_MS      2
#define LVGL_FB_ROWS      40   /* 回退②：内部 SRAM 双块块高（行） */
#define LVGL_FB_ROWS_SYNC 20   /* 回退③：内部 SRAM 单块块高（行） */
/* 🔴 2026-10-02 下载流畅度修复：UI 线程（本函数所在的 main 任务）优先级。
 * 必须 > 所有「下载/落盘/拉列表」worker（install_task=4, dl_sink=5, devlink=4, dl_seg=4），
 * 但必须 < Wi-Fi/TCPIP 协议栈（≥18），否则会饿死网络。见 sdgoods_lvgl_loop 头部说明。 */
#define LVGL_TASK_PRIO    6

/* 🔴 2026-10-02 卡顿探针：把「卡不卡」从主观感受变成**串口可测的数字**。
 * 本循环正常节奏 ≈ 1 tick = 10ms（见循环尾 vTaskDelay 的说明）。若某一轮间隔远超此值，
 * 说明 UI 线程被抢占 / 被某次 flash 事务抽走 cache ⇒ 就是肉眼看到的卡顿。
 * 只在「间隔 > LVGL_JANK_MS」时打印，并限流到 ≥1s 一条 ⇒ 顺滑时完全静默、不刷屏。
 * 判据用法：下载中看 `lvgl jank:` 的条数/最坏值；与不下载时对比即可量化改善。 */
#define LVGL_JANK_MS      40

/* 🔴 2026-10-02 UI 活动闸门（配套下载流畅度）：flush 之后多久算「UI 仍热」。
 * 见 sdgoods_lvgl.h 的 sdgoods_ui_busy() 契约。
 * 取 60 ms 的推导（别随手调大）：
 *   · LVGL 默认刷新周期 LV_DISP_DEF_REFR_PERIOD = 30 ms ⇒ 动画期间帧间隔 ≤30 ms，
 *     60 ms 窗保证「连续动画」被整段判定为忙，中间不会掉出去；
 *   · 而静止期只有「进度环每 1 % 大约 0.3 s 一次」这种零星 flush ⇒ 每次只多占 60 ms，
 *     占空比损失 ≈ 6~18 %（调大到 150 ms 会变成 45 %，等于白白拖慢下载）。 */
#define LVGL_UI_IDLE_MS   60

/* 🔴 2026-10-02 预擦让路（配套）：手指抬起后多久还算「用户正在操作」。
 * 契约见 sdgoods_lvgl.h 的 sdgoods_ui_interactive()。
 * 取 400 ms 的推导：LVGL 滚动动量（松手后惯性）与弹窗开合动效都在 200~400 ms 量级，
 * 太小会让惯性尾声被冻结切断，太大则让预擦在用户已经停手后还在白等。 */
#define LVGL_UI_TOUCH_HOLD_MS  400

static const char *TAG = "sdgoods_lvgl";
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static bool s_flush_async = true;   /* 双缓冲（PSRAM 全屏或内部 SRAM）时为 true；单块回退时 false */
static uint32_t s_flush_fail = 0;   /* 异步提交失败计数（弹跳预算不足时会持续增长，用于诊断） */

/* UI 活动闸门状态（契约见 sdgoods_lvgl.h）。全部只在 LVGL 线程写、任意任务读。 */
static volatile int64_t  s_ui_draw_us = 0;   /* 最近一次 flush 的时刻（0 = 开机以来还没画过） */
static volatile bool     s_ui_busy    = false;
static volatile uint16_t s_ui_anim_n  = 0;   /* 最近一次 lv_timer_handler 之后仍在跑的动画数 */
static volatile int64_t  s_ui_touch_us = 0;  /* 最近一次「手指按下」的时刻（0 = 从未按过） */
static volatile bool     s_ui_interactive = false;

bool sdgoods_ui_busy(void)
{
    return s_ui_busy;
}

bool sdgoods_ui_interactive(void)
{
    return s_ui_interactive;
}

/* 卡顿探针状态（见 LVGL_JANK_MS 说明） */
static int64_t  s_jank_prev_us  = 0;   /* 上一轮循环的时刻 */
static int64_t  s_jank_log_us   = 0;   /* 上次打印的时刻（限流用） */
static uint32_t s_jank_n        = 0;   /* 累计卡顿次数（间隔 > LVGL_JANK_MS） */
static int64_t  s_jank_worst_ms = 0;   /* 最坏间隔（ms） */
static int64_t  s_self_prev_us  = 0;   /* 上一轮「自用时间」= lv_timer_handler + apps_poll */

/* 🔴 2026-10-02「连着网滑动卡顿」专项：**滑动跟手度剖析**（见 LVGL_PROF_HITCH_MS）。
 * 与上面那个卡顿计数器的分工：计数器只回答「有没有 >40ms 的坑」，本剖析回答
 * 「一整段交互里每拍多长、其中多少是 UI 自己花的」——后者才是"跟不跟手"的判据。
 * 只在 s_ui_interactive（手指按住 / 松开 400ms 内）这一段累计，段一结束打一行汇总 ⇒
 * 不动手时零日志。为什么必须区分 gap 与 self（两列都要看）：
 *   · self 大  ⇒ UI 自己干多了（重建主页 / 解码封面 / 重绘 / 卡在阻塞的日志写里）；
 *   · self 小、gap 大 ⇒ 被**外部**抢走：Wi-Fi 中断、其它核/flash 事务关 cache、
 *     高优先级任务；这类只能靠挪核/降频次解决，改 UI 代码是白费力气。
 * ⚠️ 两个量描述的是**同一段区间**：gap 量的是「上一拍开头 → 本拍开头」，而 self 是
 *    上一拍内部测的，所以配对成立（别把它们当成「本拍的两个量」去重建时间线）。 */
#define LVGL_PROF_HITCH_MS 45   /* 单拍超过这个时长算一次"看得见的顿"（自然节拍 ≈30ms）*/
static bool     s_prof_on       = false;  /* 是否处于交互统计段（= 上一拍的 s_ui_interactive）*/
static uint32_t s_prof_frames   = 0;
static uint32_t s_prof_hitch    = 0;
static int64_t  s_prof_sum_gap  = 0;      /* us */
static int64_t  s_prof_sum_self = 0;      /* us */
static int64_t  s_prof_max_gap  = 0;
static int64_t  s_prof_max_self = 0;
static int64_t  s_prof_sum_tmr  = 0;      /* us：lv_timer_handler 的累计 */
static int64_t  s_prof_sum_poll = 0;      /* us：apps_poll 的累计 */
static int64_t  s_prof_max_tmr  = 0;
static int64_t  s_prof_max_poll = 0;
/* 「自用时间」再拆两半：lv_timer_handler（渲染）与 apps_poll（各应用的定时器）。
 * 为什么要拆：两者都是 UI 线程里的活，但**修法完全不同**（前者=重绘/解码，后者=应用逻辑）。 */
static int64_t  s_tmr_prev_us   = 0;
static int64_t  s_poll_prev_us  = 0;
/* 刷屏像素账（判「重绘面积太大」还是「单像素太贵」的唯一入口，见 prof 汇总行）：
 * 一帧全屏 = 360×360 = 129600 px。若交互段里平均每帧刷几十万 px，说明**有东西在反复
 * 全屏失效**（先查是谁在 invalidate）；若只有几万 px 却要几十 ms，说明**每像素太贵**
 * （大图贴图 / 带 alpha 的 PSRAM 图 / 逐像素蒙版）。 */
static uint32_t s_flush_px      = 0;      /* 本拍刷的像素数 */
static uint32_t s_flush_n       = 0;      /* 本拍 flush 次数（>1 ⇒ 一帧被切成多块）*/
static uint32_t s_prof_px       = 0;
static uint32_t s_prof_flush    = 0;
/* 段**开始时**（提速档已生效那一刻）读到的实际 timer period。
 * 🔴 为什么不在打日志时现读（2026-10-03 第一版就是这么写的、量出来恒为 30/30）：
 *    本段收尾那一拍 `s_ui_interactive` 已经翻成 false ⇒ `lvgl_boost_apply(false)` 先跑，
 *    把三个 timer 都还原成空闲档，**然后**才轮到打 `swipe prof` 行 ⇒ 现读只会读到还原后的值。
 *    这是"判据必须在正确的时刻取"的又一例。段起那一拍取一次、存下来即可。 */
static uint32_t s_prof_indev_ms = 0;
static uint32_t s_prof_refr_ms  = 0;
/* 刷屏耗时（把「渲染」再拆一刀：画像素 vs 把像素送进屏）。
 * 为什么必须拆：两者修法完全不同 —— 前者要少画/少贴图，后者要查 SPI 带宽与等信号的阻塞。 */
static int64_t  s_flush_us_acc  = 0;      /* 本拍累计刷屏耗时（us）*/
static int64_t  s_flush_us_prev = 0;      /* 上一拍（与 gap/self 同区间）*/
static int64_t  s_prof_sum_fl   = 0;
static int64_t  s_prof_max_fl   = 0;

/* ================== 🔴 2026-10-02 交互期「四档提速」（滑动跟手度专项）==================
 *
 * 【问题】用户报「连着 Wi-Fi、没下载，滑动也卡」。真机探针把三段都量出来了，根因是
 *   **整条链路都按 30ms 的节奏跑**，与 Wi-Fi 无关：
 *     · 位置更新 30ms 一次 —— LVGL 的**输入采样**周期 `CONFIG_LV_INDEV_DEF_READ_PERIOD=30`
 *       （`lv_hal_indev.c:96` 拿它建 read_timer）。手指在动、LVGL 每 30ms 才读一次坐标
 *       ⇒ 内容 33Hz 一格一格跳，这就是"不跟手"的第一因（与渲染快慢无关！）。
 *     · 上屏 30ms 一次 —— `CONFIG_LV_DISP_DEF_REFR_PERIOD=30`（刷新定时器周期）。
 *     · 主循环 10ms 一拍 —— `CONFIG_FREERTOS_HZ=100` ⇒ `pdMS_TO_TICKS(2)==0`，
 *       循环尾的 `vTaskDelay(1)` 是 **1 tick = 10ms 的硬地板**（见循环尾那段注释）。
 *       它把「渲染 14ms 的那一拍」拉成 24ms，让上面两个 30ms 周期只能被**量化到 30~38ms**。
 *     实测（row 滑动）：`swipe prof: gap avg=16ms | tmr avg=7ms | fl avg=5ms`，刷新 24 次/688ms
 *       ≈ 29ms 一次 —— 刚好卡在 30ms 上，且每次都要多等半拍。
 *
 * 【修法】**只在交互期**（手指按住 + 松开后 LVGL_UI_TOUCH_HOLD_MS 内）把三档同时调快，
 *   段一结束立刻还原成省电/低占用的默认值。三个值都是**运行时改定时器周期**，
 *   不动 sdkconfig、不动 lv_conf，因而不影响任何其它界面与下载链路的既有节奏。
 *   ⚠️ 三个必须一起调：只调输入 ⇒ 渲染跟不上、跳得更明显（位置变了没人画）；
 *     只调渲染 ⇒ 白刷（坐标没变）；不调主循环 ⇒ 上面两个周期被 10ms 地板量化，白调。
 *   ⚠️ 退出时**必须还原**（`lvgl_boost_apply(false)`）—— 空闲时保持 30/30/10ms，
 *     CPU0 占用与功耗与改动前完全一致；也避免下载/长按等场景被无谓地抢 CPU。
 *   ⚠️ 提速只影响「多久看一次/画一次」，**不改任何 UI 常量与布局** ⇒ 不会动像素。
 *
 * 🔴 2026-10-03 补第 4 档（起因：用户报「控制中心上下拖动感觉有点掉帧」）——**动画推进周期**。
 *   上面的「上屏刷新」提速只解决"多久把屏幕重画一次"，但 LVGL 的动画**由另一个内部定时器
 *   独立驱动**：`lv_anim.c:60` 里 `_lv_anim_tmr = lv_timer_create(anim_timer,
 *   LV_DISP_DEF_REFR_PERIOD, NULL)` —— 周期在建核时就被抄成 30ms，**全工程没有任何地方
 *   会去改它**（它与 disp 的 refr_timer 是两颗互不相干的 timer）。
 *   ⇒ 位移**数值**本身仍 30ms 才更新一次：把上屏提到 16ms 也是白刷，画的还是那 6 个位置
 *     （360px 的行程每帧跳 60px）⇒ 肉眼就是"掉帧"。
 *   🔴 **判据修正（通用）：一个 `lv_anim` 动画的「有效帧率 = anim_timer 周期」，不是 refr 周期。**
 *     所以凡是"靠 lv_anim 驱动的位移/缩放"类动画，光提速上屏都没有用。
 *   ⚠️ 安全性（已读源码核对）：`anim_timer` 只用 `lv_tick_elaps(last_timer_run)` 推进
 *     （不假设周期）；且 `last_timer_run` 在 `anim_mark_list_change()` 里会被重置，而该函数
 *     在定时器 resume 时必被调用 ⇒ **改周期只改帧数，不改动画总时长，也不会让第一帧跳一大步**。
 *   ⚠️ 取 16ms（与上屏同档）：两者同周期 ⇒ 在**同一拍 `lv_timer_handler` 里**先后到期
 *     （anim 先更新位移、refr 紧接着画），不会出现"这一拍画的是上一拍位置"的相位浪费。 */
#define LVGL_BOOST_INDEV_MS   10   /* 输入采样：30 → 10（= 触摸控制器自身的 active 轮询率）*/
#define LVGL_BOOST_REFR_MS    16   /* 上屏：30 → 16（实测每帧 ~14ms ⇒ 目标 ~50-60fps）*/
#define LVGL_BOOST_ANIM_MS    16   /* 动画推进：30 → 16（第 4 档，见上）*/
#define LVGL_BOOST_TICK_MS    2    /* 交互期循环唤醒周期（= lvgl_tick_cb 的心跳）*/
#define LVGL_IDLE_INDEV_MS    CONFIG_LV_INDEV_DEF_READ_PERIOD
#define LVGL_IDLE_REFR_MS     CONFIG_LV_DISP_DEF_REFR_PERIOD
#define LVGL_IDLE_ANIM_MS     CONFIG_LV_DISP_DEF_REFR_PERIOD

static TaskHandle_t s_ui_task = NULL;     /* 跑 sdgoods_lvgl_loop 的那个任务（rx 心跳唤醒用）*/
static bool         s_boost_on = false;   /* 当前是否处于提速档（= 上一拍的 s_ui_interactive）*/
static bool         s_anim_boost_en = true; /* 第 4 档（动画推进）是否参与提速：A/B 开关，默认开 */
static int64_t      s_poll_last_us = 0;   /* 上次调 sdgoods_apps_poll 的时刻（最小间隔闸门）*/

/* 把「上屏刷新周期」改成 ms。LVGL 8.3 的刷新由一个内部 lv_timer 驱动，
 * 周期就是建屏时从 LV_DISP_DEF_REFR_PERIOD 抄来的那个值；这里运行时覆盖它。 */
static void lvgl_refr_period_set(uint32_t ms)
{
    lv_timer_t *t = _lv_disp_get_refr_timer(lv_disp_get_default());
    if (t && t->period != ms) {
        lv_timer_set_period(t, ms);
    }
}

/* 把「输入采样周期」改成 ms。所有已注册的 indev 一起改（本机只有触摸一路）。 */
static void lvgl_indev_period_set(uint32_t ms)
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (indev->driver && indev->driver->read_timer &&
            indev->driver->read_timer->period != ms) {
            lv_timer_set_period(indev->driver->read_timer, ms);
        }
    }
}

/* 把「动画推进周期」改成 ms（第 4 档，2026-10-03 补）。
 * 🔴 这是**与上屏刷新完全独立的另一颗 timer**：LVGL 的 `lv_anim` 全部由它驱动。
 *    不改它的话，任何 lv_anim 动画（位移、淡入淡出、缩放）都只有 33Hz 的更新率 ——
 *    refr 提到 16ms 也只是把同一张画面画两遍。
 * ⚠️ 用公开接口 `lv_anim_get_timer()`（lv_anim.h:367）拿句柄，不碰 LVGL 内部符号。
 * ⚠️ 无动画在跑时这颗 timer 是 paused 的，改周期不会让它空转。 */
static void lvgl_anim_period_set(uint32_t ms)
{
    lv_timer_t *t = lv_anim_get_timer();
    if (t && t->period != ms) {
        lv_timer_set_period(t, ms);
    }
}

/* 读回**当前真正生效**的周期（判据用）。
 * 🔴 为什么必须有这两个：`interactive boost ON` 那行原来打的是 `LVGL_BOOST_*` **宏**
 *    （= 期望值），而 2026-10-03 的合成手势标定发现"每拍 ~32ms"，与宏宣称的 10ms 对不上
 *    ⇒ 属于本项目反复踩到的「**回显不是实际值**」那一类。现在两者都改成读
 *    `lv_timer_t->period` 的**实际值**，与宏无关（宏改了、set 没生效，一眼能看出来）。 */
static uint32_t lvgl_indev_period_now(void)
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (indev->driver && indev->driver->read_timer) {
            return indev->driver->read_timer->period;
        }
    }
    return 0;   /* 没有输入设备 */
}

static uint32_t lvgl_refr_period_now(void)
{
    lv_timer_t *t = _lv_disp_get_refr_timer(lv_disp_get_default());
    return t ? t->period : 0;
}

/* 交互期提速档的总闸门（进入/退出各调一次，幂等）。 */
static void lvgl_boost_apply(bool on)
{
    if (s_boost_on == on) {
        return;
    }
    s_boost_on = on;
    if (on) {
        lvgl_indev_period_set(LVGL_BOOST_INDEV_MS);
        lvgl_refr_period_set(LVGL_BOOST_REFR_MS);
    } else {
        lvgl_indev_period_set(LVGL_IDLE_INDEV_MS);
        lvgl_refr_period_set(LVGL_IDLE_REFR_MS);
    }
    /* 🔴 第 4 档**单独**走 s_anim_boost_en（A/B 开关）⇒ 不并进上面那两个分支，
     *    否则关掉开关后它又会被 on=true 拉回提速值。 */
    lvgl_anim_period_set((on && s_anim_boost_en) ? LVGL_BOOST_ANIM_MS : LVGL_IDLE_ANIM_MS);
    /* 🔴 2026-10-03：三个数字**全部改成读实际 period**（原来是宏 = 期望值）。
     *    理由见 `lvgl_indev_period_now()` 上方。判"提速有没有真的生效"只认这一行。 */
    ESP_LOGI(TAG, "interactive boost %s: indev=%ums refr=%ums anim=%ums loop=%s "
                  "(target 10/16/16)",
             on ? "ON" : "OFF",
             (unsigned)lvgl_indev_period_now(),
             (unsigned)lvgl_refr_period_now(),
             (unsigned)lv_anim_get_timer()->period,
             on ? "2ms heartbeat" : "vTaskDelay(1tick=10ms)");
}

/* 第 4 档的运行时开关（默认开）。**只给 A/B 验收用**（串口 `{anim0!` / `{anim1!`）：
 *   0 ⇒ 动画定时器保持 30ms（= 提速前的行为） 1 ⇒ 提到 16ms。
 * 🔴 为什么需要它：动画帧数是"定时器周期"的函数，两次烧录对比会被环境噪声干扰；
 *    同固件里现场切档 ⇒ 同一个手势、同一段 Wi-Fi 负载下，帧数一眼可比。 */
void sdgoods_lvgl_anim_boost_enable(bool en)
{
    s_anim_boost_en = en;
    /* 立刻生效：apply 是幂等的、改开关不会触发它 ⇒ 这里自己重设一次。 */
    const uint32_t ms = (s_boost_on && en) ? LVGL_BOOST_ANIM_MS : LVGL_IDLE_ANIM_MS;
    lvgl_anim_period_set(ms);
    ESP_LOGI(TAG, "anim boost %s -> anim timer %dms", en ? "ON" : "OFF", (int)ms);
}

/* ==================== 跨任务 → LVGL 线程的投递队列 ====================
 * 契约与「为什么不能用 lv_async_call」的完整说明见 sdgoods_lvgl.h。
 * 这里只强调实现上的两条硬约束：
 *   ① 生产者在**任意任务**、消费者只在 LVGL 线程 ⇒ 进出都要临界区（多生产者 / 单消费者）；
 *   ② 回调必须**在临界区之外**执行 —— 回调里会 lv_obj_create/lv_obj_del，
 *      在临界区里做这些会拉长关中断时间，且 LVGL 内部可能再次进入临界区。 */
#define LVGL_POST_Q_N 8

typedef struct {
    sdgoods_lvgl_cb_t cb;
    void *arg;
} lvgl_post_item_t;

static lvgl_post_item_t s_post_q[LVGL_POST_Q_N];
static volatile uint8_t s_post_head;   /* 生产者推进（写） */
static volatile uint8_t s_post_tail;   /* 消费者推进（LVGL 线程） */
static portMUX_TYPE s_post_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_post_drop = 0;

bool sdgoods_lvgl_post(sdgoods_lvgl_cb_t cb, void *arg)
{
    if (!cb) {
        return false;
    }
    bool ok = false;
    portENTER_CRITICAL(&s_post_mux);
    uint8_t n = (uint8_t)((s_post_head + 1) % LVGL_POST_Q_N);
    if (n != s_post_tail) {                 /* 留一格空位区分「满」与「空」 */
        s_post_q[s_post_head].cb  = cb;
        s_post_q[s_post_head].arg = arg;
        s_post_head = n;
        ok = true;
    }
    portEXIT_CRITICAL(&s_post_mux);

    if (!ok) {
        s_post_drop++;
        if (s_post_drop == 1 || (s_post_drop % 32) == 0) {
            ESP_LOGW(TAG, "lvgl post queue full, dropped %u request(s) so far",
                     (unsigned)s_post_drop);
        }
    }
    return ok;
}

/* LVGL 线程每轮执行：取出并执行所有已投递的闭包。
 * 每轮上限 = 队列深度 × 4：纯粹防「回调里 self-post」把 lv_timer_handler 饿死，
 * 正常路径永远不会触及（生产端只有串口任务，一次最多投一两个）。 */
static void lvgl_post_drain(void)
{
    int budget = LVGL_POST_Q_N * 4;

    while (budget-- > 0) {
        sdgoods_lvgl_cb_t cb  = NULL;
        void             *arg = NULL;

        portENTER_CRITICAL(&s_post_mux);
        if (s_post_tail != s_post_head) {
            cb  = s_post_q[s_post_tail].cb;
            arg = s_post_q[s_post_tail].arg;
            s_post_q[s_post_tail].cb  = NULL;
            s_post_q[s_post_tail].arg = NULL;
            s_post_tail = (uint8_t)((s_post_tail + 1) % LVGL_POST_Q_N);
        }
        portEXIT_CRITICAL(&s_post_mux);

        if (!cb) {
            break;   /* 已排空 */
        }
        cb(arg);     /* ⚠️ 临界区外：回调里会建/删 LVGL 对象树 */
    }
}

static void lvgl_flush_cb_async(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    (void)drv;

    /* ============================================================================
     * 🔴 2026-10-02（下载期卡顿第三轮定案）：**PSRAM 源不再直接交给面板驱动**。
     *
     * 【为什么】面板驱动把一次 draw 拆成 4092 B 分片；PSRAM 不在 S3 的 DMA 窗口
     * ⇒ spi_master 要为**每一个分片**临时 malloc 一块内部 DMA 弹跳。而总线会把
     * max_transfer_sz **向上取整到 DMA 描述符粒度（4092B）**，所以弹跳**缩不小**
     * （见 sdgoods_lcd.h 的 2026-09-20 实测纠正）。
     * 下载期 Wi-Fi 动态缓冲 + TLS 在同一个堆上反复拿/还，实测把内部 DMA 的最大
     * 连续块从开机 31744 B 打碎到 **2688~3968 B** ⇒ 每片弹跳必失败
     * （`async flush submit failed (err=0x101)`，一次 50s 下载里 ≥360 次）
     * ⇒ **每一帧都退回同步「弹跳带」路径**（见下面的 fallback），而带路径每带都要
     * 同步等完成信号量 ⇒ 又叠加了 500ms 级阻塞。用户看到的就是「下载时滑动/弹窗卡」。
     *
     * 【修法】PSRAM 源**统一走「预留的内部 DMA 弹跳带」**（sdgoods_lcd.c 的
     * sdgoods_lcd_draw_bitmap_safe：分带 memcpy 到开机占住、永不释放的 8KB 内部
     * DMA 缓冲，再从它发 SPI）。从此刷屏**与堆碎片彻底无关**，异步路径不再失败、
     * 也就没有"每帧回退"。
     *
     * 【代价与取舍】本帧不再与渲染重叠（同步分带发送），帧时间 = 渲染 + SPI 传输。
     * 全屏 259200 B @ ~40MB/s ≈ 6.5 ms，加渲染仍在 17 ms（60fps）以内 ——
     * 用这点余量换掉「每帧必然回退 + 500ms 阻塞」是合算的。
     * ⚠️ 别再改回「PSRAM 直交 esp_lcd_panel_draw_bitmap」：那条路的失败与堆碎片
     *    强耦合，且失败后驱动内部 inflight 计数会失衡，症状极难定位。
     * ============================================================================ */
    if (esp_ptr_external_ram(color_map)) {
        const esp_err_t e = sdgoods_lcd_draw_bitmap_safe(area->x1, area->y1,
                                                         area->x2 + 1, area->y2 + 1, color_map);
        if (e != ESP_OK) {
            if (s_flush_fail == 0 || (s_flush_fail % 120) == 0) {
                ESP_LOGW(TAG, "staged flush failed (err=0x%x, n=%u)", (int)e, (unsigned)s_flush_fail);
            }
            s_flush_fail++;
        }
        /* 分带发送是本函数内同步完成的（sdgoods_lcd 内部已逐带等完成）⇒ 这里直接放行。 */
        lv_disp_flush_ready(drv);
        return;
    }

    /* 内部 SRAM 缓冲（回退路径 ②/③，仅当 PSRAM 拿不到全屏双块时才出现）：
     * 源本身就在 DMA 窗口内 ⇒ 交驱动不会要弹跳、不存在上面的失败模式，
     * 保留原有的**异步**提交（仅把数据交给 DMA 就返回，渲染与传输重叠）。 */
    esp_err_t async_err = esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (async_err == ESP_OK) {
        return;   /* 提交成功：DMA 完成后 on_color_done 会通知 LVGL，此处绝不调 flush_ready */
    }
    /* 异步提交失败（如 SPI 事务队列暂时溢出返回 ESP_ERR_NO_MEM）：
     * on_color_done 不会再触发，必须由本回调亲自通知 LVGL 释放绘制缓冲。否则
     * draw_buf->flushing 恒为 1，LVGL 在 lv_refr.c:710 死等 -> 看门狗死机
     * （即「CC 打开点音量/亮度进二级页 → 死机」的根因）。
     * 先尽力用同步兜底把画面送出去（成功则本帧照常上屏），无论如何最后都调
     * lv_disp_flush_ready，保证 flushing 一定被清零——即使本帧丢块，也只表现为
     * 一次性花屏，而非整机死机。 */
    /* 限流打印 + 计数：若此日志持续出现，说明弹跳预算仍不足
     * （LCD_SPI_TRANS_QUEUE_SZ × LCD_SPI_MAX_TRANSFER_SIZE > 内部 DMA 堆）。 */
    if (s_flush_fail == 0 || (s_flush_fail % 120) == 0) {
        ESP_LOGW(TAG, "async flush submit failed (err=0x%x, n=%u), fallback sync",
                 (int)async_err, (unsigned)s_flush_fail);
    }
    s_flush_fail++;
    sdgoods_lcd_draw_bitmap_safe(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_flush_cb_sync(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* 回退模式（内部 SRAM 单块）：同步 flush，安全但较慢 */
    sdgoods_lcd_draw_bitmap_safe(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* UI 活动闸门：本帧确实画了东西（同步/异步两条路都经过这里）⇒ 记时刻。
     * 供 `sdgoods_ui_busy()` 判「UI 刚重绘过」（该档当前无消费者，
     * 下载写盘闸门已改用只看触摸的 `sdgoods_ui_interactive()`）。 */
    const int64_t t_fl0 = esp_timer_get_time();
    s_ui_draw_us = t_fl0;
    /* 像素账（判据见 s_flush_px 的说明）：本拍/本交互段刷了多少像素。
     * 放在两条路**之前** ⇒ 同步/异步都记到，不受回退路径影响。 */
    const uint32_t px = (uint32_t)((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
    s_flush_px += px;
    s_flush_n++;
    if (s_prof_on) {
        s_prof_px    += px;
        s_prof_flush += 1;
    }
    if (s_flush_async) {
        lvgl_flush_cb_async(drv, area, color_map);
    } else {
        lvgl_flush_cb_sync(drv, area, color_map);
    }
    /* 刷屏耗时（判据见 s_flush_us_acc 的说明）：含分带 memcpy + 等每带完成信号量。
     * 记在**两条路之后** ⇒ 同步/异步都算进去。 */
    const int64_t fl = esp_timer_get_time() - t_fl0;
    s_flush_us_acc += fl;
    if (s_prof_on) {
        s_prof_sum_fl += fl;
        if (fl > s_prof_max_fl) { s_prof_max_fl = fl; }
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
    /* 交互期「2ms 心跳」：顺手叫醒 UI 任务，让它能按 ≤2ms 的粒度决定「现在要不要画」。
     * 为什么走这里而不是再开一个 esp_timer：本回调本来就已经每 LVGL_TICK_MS(2ms) 跑一次，
     * 白搭一个定时器等于多一份中断开销。
     * ⚠️ 任务不在等通知时 `xTaskNotifyGive` 只累加计数、**不会**把它从 vTaskDelay 里唤醒
     *   （FreeRTOS 只在 ucNotifyState==taskWAITING_NOTIFICATION 时才 unblock）
     *   ⇒ 空闲期这一句是纯白给，不改变原有节奏。
     * ⚠️ 本回调跑在 esp_timer 任务上下文（prio 22），不是中断，可以直接用普通 API。 */
    if (s_ui_task) {
        xTaskNotifyGive(s_ui_task);
    }
}

void sdgoods_lvgl_init(void)
{
    /* ★ 内存策略：把「大于 128 字节」的通用 malloc 一律改走 PSRAM。
     *
     * 为什么必须这么做（2026-09-19 真机逐层定位，结论有日志支撑）：
     *   本工程默认 CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384 —— 小于 16KB 的
     *   通用 malloc **优先吃内部 RAM**。而 LVGL 的对象/样式（LV_MEM_CUSTOM=y
     *   ⇒ stdlib malloc）几乎全落在这一区间，于是界面一建就把内部 RAM 吃掉：
     *     启动时内部 DMA 最大连续块 9728B
     *       → 建完首页 UI 只剩 2304B（整屏 flush 失败）
     *       → 唤出控制中心只剩 184B（连 4096B 分片都拿不到）
     *   而 PSRAM 绘制缓冲（全屏 259200B）的**每个分片**都要一块内部 DMA
     *   弹跳缓冲（PSRAM 不在 DMA 窗口内，spi_master 必须拷到内部 RAM 才能发），
     *   分配失败即 0x101 → draw_bitmap 整块失败 → 该帧屏幕不更新。
     *   用户可见症状：「app 下滑没有控制中心」；整屏都失败时就是「无法打开 app0」。
     *
     *   阈值取 0：**所有**通用 malloc 都走 PSRAM（8MB 富余），内部 DMA 完整
     *   留给 SPI 弹跳。实测取 128 时仍不够 —— LVGL 单个对象虽 >128B，但样式、
     *   字符串、坐标数组等大量小分配仍在内部，累计又吃掉 5KB（UI 建完内部
     *   最大连续块只剩 4096B，而 2 个在途分片需要 8192B）。
     * ⚠️ 别把阈值调回 16384（或删掉本行）：本 bug 会立刻复发。
     * ⚠️ 必须在 lv_init() 之前调用：lv_init 自身及随后的界面创建都要受影响。 */
    heap_caps_malloc_extmem_enable(0);
    ESP_LOGI(TAG, "malloc extmem threshold = 0B (all malloc -> PSRAM, internal DMA kept for SPI bounce)");

    lv_init();

    /* 诊断：内部 DMA 堆最大连续空闲块，用于判断能否放下内部 SRAM 缓冲（全屏肯定放不下） */
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "internal DMA largest free block: %d bytes", (int)dma_largest);

    lv_color_t *buf1 = NULL;
    lv_color_t *buf2 = NULL;
    uint32_t buf_px = 0;

    /* 路径 ①（默认）：全屏 PSRAM 双块 + 异步 flush —— 1 flush/帧，杜绝横滑「断成 3 段」。
     * 只请求 MALLOC_CAP_SPIRAM：S3 的 PSRAM 堆不带 MALLOC_CAP_DMA（PSRAM 不在 DMA 窗口内，
     * 由 spi_master 自动弹跳拷贝），若同时要求 DMA 反而可能匹配不到任何堆而返回 NULL。 */
    size_t full_bytes = (size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t);
    buf1 = heap_caps_aligned_alloc(32, full_bytes, MALLOC_CAP_SPIRAM);
    buf2 = buf1 ? heap_caps_aligned_alloc(32, full_bytes, MALLOC_CAP_SPIRAM) : NULL;
    if (buf1 && buf2) {
        s_flush_async = true;
        buf_px = (uint32_t)((size_t)LCD_WIDTH * LCD_HEIGHT);
        ESP_LOGI(TAG, "draw buffer: DOUBLE full-frame PSRAM (async, 1 flush/frame), ext1=%d ext2=%d bytes=%d",
                 (int)esp_ptr_external_ram(buf1), (int)esp_ptr_external_ram(buf2), (int)full_bytes);
    } else {
        if (buf2) heap_caps_free(buf2);
        if (buf1) heap_caps_free(buf1);
        buf1 = buf2 = NULL;

        /* 路径 ②：内部 SRAM 40 行双块 + 异步（PSRAM 不足时回退；横滑会有轻微横带）。 */
        size_t buf_bytes = (size_t)LCD_WIDTH * LVGL_FB_ROWS * sizeof(lv_color_t);
        buf1 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        buf2 = buf1 ? heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : NULL;
        if (buf1 && buf2) {
            s_flush_async = true;
            buf_px = (uint32_t)((size_t)LCD_WIDTH * LVGL_FB_ROWS);
            ESP_LOGW(TAG, "draw buffer (fallback): DOUBLE %d-row INTERNAL SRAM (async), bytes=%d",
                     (int)LVGL_FB_ROWS, (int)buf_bytes);
        } else {
            /* 路径 ③：内部 SRAM 20 行单块 + 同步 flush（极少数，稳但不快）。 */
            if (buf1) heap_caps_free(buf1);
            if (buf2) heap_caps_free(buf2);
            buf_bytes = (size_t)LCD_WIDTH * LVGL_FB_ROWS_SYNC * sizeof(lv_color_t);
            buf1 = heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            buf2 = NULL;
            s_flush_async = false;
            buf_px = (uint32_t)((size_t)LCD_WIDTH * LVGL_FB_ROWS_SYNC);
            if (buf1) {
                ESP_LOGW(TAG, "draw buffer (fallback): SINGLE %d-row INTERNAL SRAM (sync), bytes=%d",
                         (int)LVGL_FB_ROWS_SYNC, (int)buf_bytes);
            } else {
                ESP_LOGE(TAG, "draw buffer alloc failed");
                abort();
            }
        }
    }

    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_WIDTH;
    s_disp_drv.ver_res = LCD_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&s_disp_drv);
    /* 让 SPI DMA 完成回调能在刷完后通知 LVGL（异步 flush 模式下必需） */
    sdgoods_lcd_set_lvgl_drv(&s_disp_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));
}

void sdgoods_lvgl_loop(void)
{
    /* 🔴 2026-10-02 下载流畅度修复：把 UI 线程（= 跑本函数的 main 任务）优先级抬到 6。
     * 为什么：下载时 install_task(4)/dl_sink(5)/devlink(4)/dl_seg(4) 这些 worker 就绪频率高，
     * 原本 UI 线程（默认 prio 1）被持续抢占 ⇒ lv_timer_handler() 的渲染帧被切碎 ⇒
     * 主页左右滑动、弹窗开关明显卡顿。
     * 修复后：本循环每轮仍 vTaskDelay(10ms) 让出 CPU，worker 只在那 10ms 空隙里跑，
     * 渲染帧不再被抢占；下载吞吐不受影响（60KB/s 下 10ms 调度延迟 < 4%，且 sink 队列
     * 160KB 缓冲兜底）。刻意只抬到 6：低于 Wi-Fi/TCPIP 协议栈（≥18），不会饿死网络。
     * 同批的另一处（配套）：worker 钉 CPU1 + Wi-Fi/TCPIP 也挪到 CPU1（sdkconfig）
     *   ⇒ CPU0 基本只跑 UI，高优先级的网络/落盘任务不再切开渲染帧。
     * ⚠️ **已知残留（2026-10-02 实测，尚未消除）**：写 flash 时 SPI1 与 cache 互斥，
     *   全芯片「关 cache + 关中断」⇒ 渲染帧仍会被打断。实测下载 1.6MB 期间本探针仍
     *   抓到 ~86 次 >40ms 的间隔（60~150ms），且它同时把网络读拖慢（同一条链路
     *   纯网络 73.5 KB/s，真实安装只有 18.7 KB/s）。
     *   治本只能靠 flash auto-suspend，而本机颗粒（GigaDevice GD25Q256）IDF 无 suspend
     *   驱动（`E spi_flash: Suspend and resume may not supported for this flash model yet.`
     *   + 反复 rst:0xc，已试并回退）。试过把 OTA 写缓冲经 DRAM 暂存成大分片
     *   （拉长单次关 cache 窗口）⇒ 未见改善且有丢包风险，**已回退**。 */
    vTaskPrioritySet(xTaskGetCurrentTaskHandle(), LVGL_TASK_PRIO);
    ESP_LOGI(TAG, "lvgl loop task priority raised to %d (above download/sink workers, below net stack)",
             (int)LVGL_TASK_PRIO);
    /* 交给 lvgl_tick_cb 当「叫醒目标」用（见 LVGL_BOOST_* 那段与 lvgl_tick_cb 的说明）。 */
    s_ui_task = xTaskGetCurrentTaskHandle();

    while (1) {
        /* ---- 卡顿探针（判据见 LVGL_JANK_MS）：量本轮与上一轮的间隔 ---- */
        const int64_t now_us = esp_timer_get_time();
        if (s_jank_prev_us != 0) {
            const int64_t gap_ms  = (now_us - s_jank_prev_us) / 1000;
            const int64_t self_ms = s_self_prev_us / 1000;
            const uint32_t px_prev = s_flush_px;   /* 上一拍刷的像素（与 self/gap 同区间）*/
            const uint32_t fn_prev = s_flush_n;
            const int64_t  fl_ms   = s_flush_us_prev / 1000;
            if (gap_ms > LVGL_JANK_MS) {
                s_jank_n++;
                if (gap_ms > s_jank_worst_ms) {
                    s_jank_worst_ms = gap_ms;
                }
                if (now_us - s_jank_log_us >= 1000000) {   /* 限流：≥1s 一条，顺滑时静默 */
                    s_jank_log_us = now_us;
                    /* `self=` 是**分工判据**（见 LVGL_PROF_HITCH_MS 的说明）：
                     * 超过一半 ⇒ UI 自己花的（重建/解码/阻塞日志写）；否则是被外部抢的。
                     * `px=` 是**重绘面积**（129600 = 整整一屏）；`fl=` 是其中**送屏**花的，
                     * ⇒ `self - fl` 才是真正「画」出来的时间。 */
                    ESP_LOGW(TAG, "lvgl jank: gap=%lldms self=%lldms fl=%lldms px=%u/%uf worst=%lldms n=%u (%s)",
                             (long long)gap_ms, (long long)self_ms, (long long)fl_ms,
                             (unsigned)px_prev, (unsigned)fn_prev,
                             (long long)s_jank_worst_ms, (unsigned)s_jank_n,
                             (self_ms * 2 > gap_ms) ? "ui self work" : "preempted externally");
                }
            }
            /* 交互段跟手度累计（不动手时 s_prof_on 恒 false ⇒ 零开销、零日志） */
            if (s_prof_on) {
                s_prof_frames++;
                s_prof_sum_gap  += (now_us - s_jank_prev_us);
                s_prof_sum_self += s_self_prev_us;
                s_prof_sum_tmr  += s_tmr_prev_us;
                s_prof_sum_poll += s_poll_prev_us;
                if (now_us - s_jank_prev_us > s_prof_max_gap)  { s_prof_max_gap  = now_us - s_jank_prev_us; }
                if (s_self_prev_us      > s_prof_max_self)     { s_prof_max_self = s_self_prev_us; }
                if (s_tmr_prev_us       > s_prof_max_tmr)      { s_prof_max_tmr  = s_tmr_prev_us; }
                if (s_poll_prev_us      > s_prof_max_poll)     { s_prof_max_poll = s_poll_prev_us; }
                if (gap_ms > LVGL_PROF_HITCH_MS)               { s_prof_hitch++; }
            }
        }
        s_jank_prev_us  = now_us;
        s_flush_px      = 0;   /* 本拍清零：下一拍顶上读到的就是「本拍刷了多少」 */
        s_flush_n       = 0;
        s_flush_us_prev = s_flush_us_acc;
        s_flush_us_acc  = 0;

        sdgoods_power_key_poll();
        /* 跨任务投递过来的闭包：**本工程唯一允许跨任务碰 LVGL 的入口**
         * （契约见 sdgoods_lvgl.h）。放在 lv_timer_handler() 之前 ⇒ 与 lv_async_call
         * 同为「下一拍生效」，但执行点不在 LVGL 的任何内部结构上。
         * 刻意放在 suspended 判断**之外**：熄屏（背光关、跳过渲染）期间投递的请求
         * 也要被执行掉，否则队列积压到满会被丢弃。 */
        lvgl_post_drain();
        /* 熄屏低功耗：主页短按电源键进入 suspended 后，跳过渲染与轮询，
         * 背光已关闭，仅保留电源键轮询以便下次短按唤醒。大幅省电且唤醒即时。 */
        int64_t t_tmr0 = 0, t_tmr1 = 0, t_poll1 = 0;   /* 见 LVGL_PROF_HITCH_MS；熄屏全 0 */
        if (!sdgoods_power_is_suspended()) {
            t_tmr0 = esp_timer_get_time();
            lv_timer_handler();
            t_tmr1 = esp_timer_get_time();
            /* 各应用的定时器推进（主页刷新 / 游戏物理 / 蓝牙收包 / 扫描结果…）
               由应用层汇总成一个函数注册进来，新增应用只需改 apps_registry.c，
               这个平台主循环永远不用动。
               ⚠️ 2026-10-02：交互期主循环会跑到 ~2ms 一拍（见 LVGL_BOOST_*），而本钩子
                 是「调用一次推进一次」的语义（派生 app 里有游戏物理 / 蓝牙收包 / 网络
                 重试计数…）⇒ 这里加一道**最小间隔**闸门，保证调用节奏与改动前
                 （10ms 一拍）基本一致，不会因为主循环变快而把应用逻辑跑快 3~5 倍。
                 本工程（启动器）没注册 apps_poll，这里是恒 no-op。 */
            if (t_tmr1 - s_poll_last_us >= 8000) {
                s_poll_last_us = t_tmr1;
                sdgoods_apps_poll();
            }
            /* UI 活动闸门（判据见 sdgoods_lvgl.h）：动画数必须在 timer 跑完后取，
             * 否则「这一拍刚结束的动画」会被算成还在跑。 */
            s_ui_anim_n = lv_anim_count_running();
            t_poll1 = esp_timer_get_time();
        }
        /* 「本拍 UI 自用时间」拆两半（判据见 LVGL_PROF_HITCH_MS）：
         *   tmr  = lv_timer_handler ⇒ 渲染 / 失效重绘 / 封面解码 / 主页重建；
         *   poll = sdgoods_apps_poll ⇒ 应用层定时器逻辑。
         * 熄屏时不渲染 ⇒ 两者都是 0。必须在 vTaskDelay **之前**测完，
         * 否则会把 10ms 的让路算进自用时间。 */
        s_tmr_prev_us  = t_tmr1 - t_tmr0;
        s_poll_prev_us = t_poll1 - t_tmr1;
        s_self_prev_us = s_tmr_prev_us + s_poll_prev_us;
        /* 本拍汇总「UI 忙不忙」，供下载写盘任务让路。熄屏时恒 false（不渲染，无需保护）。
         * 同时汇总「**用户**在不在操作」（更严的一档，判据只有触摸）——
         * 两者差别与用途见 sdgoods_lvgl.h 的契约，别混用。 */
        const int64_t ui_now     = esp_timer_get_time();
        const bool    ui_pressed = (sdgoods_touch_get_state() == LV_INDEV_STATE_PRESSED);
        if (ui_pressed) {
            s_ui_touch_us = ui_now;
        }
        const bool ui_awake = !sdgoods_power_is_suspended();
        s_ui_busy = ui_awake &&
                    (s_ui_anim_n > 0 ||
                     ui_pressed ||
                     (ui_now - s_ui_draw_us) < (int64_t)LVGL_UI_IDLE_MS * 1000);
        s_ui_interactive = ui_awake &&
                           (ui_pressed ||
                            (ui_now - s_ui_touch_us) < (int64_t)LVGL_UI_TOUCH_HOLD_MS * 1000);
        /* ---- 交互期「三档提速」总闸（判据见 LVGL_BOOST_* 那一大段）----------------
         * 幂等：内部只在状态翻转时改定时器 ⇒ 不动手时这里是一句比较，零日志零开销。
         * ⚠️ 放在 s_ui_interactive 算完之后、本拍渲染**之前**：本拍就能按新周期跑；
         *    放后面会白白多等一拍（10ms）。 */
        lvgl_boost_apply(s_ui_interactive);
        /* ---- 交互段跟手度汇总（判据见 LVGL_PROF_HITCH_MS）：段一结束打一行 ----------
         * 为什么在「段结束」而不是每拍打：滑动时每拍都打会自己把日志通道塞满，
         * 而 USB-Serial-JTAG 的日志写是**阻塞**的 ⇒ 探针本身变成卡顿源（本工程踩过）。
         * 读法（一次滑动一行就够判定）：
         *   gap_max 远超 gap_avg        ⇒ 有可感知的顿
         *   self_max ≈ gap_max          ⇒ UI 自己干的（重建/解码/重绘/阻塞日志）
         *   self_max ≪ gap_max          ⇒ 被外部抢（Wi-Fi 中断 / flash 关 cache / 高优任务）
         *   frames 很少而跨度很长      ⇒ 触摸采样本身稀疏（先查 touch: slow read） */
        if (s_ui_interactive != s_prof_on) {
            if (s_ui_interactive) {
                s_prof_on = true;
                s_prof_frames = 0; s_prof_hitch = 0;
                s_prof_sum_gap = 0; s_prof_sum_self = 0;
                s_prof_max_gap = 0; s_prof_max_self = 0;
                s_prof_sum_tmr = 0; s_prof_sum_poll = 0;
                s_prof_max_tmr = 0; s_prof_max_poll = 0;
                s_prof_sum_fl = 0; s_prof_max_fl = 0;
                s_prof_px = 0; s_prof_flush = 0;
                /* 提速档此刻已由上面那次 `lvgl_boost_apply(true)` 落好 ⇒ 取真值存起来。
                 * 这行的存在就是为了让「滑动时到底跑在几毫秒档」在**证据行里自带**。 */
                s_prof_indev_ms = lvgl_indev_period_now();
                s_prof_refr_ms  = lvgl_refr_period_now();
            } else {
                s_prof_on = false;
                if (s_prof_frames >= 5) {
                    const int64_t avg_gap  = s_prof_sum_gap  / (int64_t)s_prof_frames;
                    const int64_t avg_tmr  = s_prof_sum_tmr  / (int64_t)s_prof_frames;
                    const int64_t avg_poll = s_prof_sum_poll / (int64_t)s_prof_frames;
                    const int64_t avg_fl   = s_prof_flush ? (s_prof_sum_fl / (int64_t)s_prof_flush) : 0;
                    /* px/frame = 每帧刷的像素数（129600 = 整整一屏）；nflush = 这一段里
                     * 真正刷屏的次数（≈ 画了几帧）。合起来才判得出是「面积太大」还是
                     * 「单像素太贵」；`tmr - fl` ≈ 纯画的时间。 */
                    const uint32_t px_per_frame = s_prof_flush ? (s_prof_px / s_prof_flush) : 0;
                    ESP_LOGI(TAG, "swipe prof: n=%u gap avg=%lldms max=%lldms | tmr avg=%lldms max=%lldms "
                                  "| poll avg=%lldms max=%lldms | fl avg=%lldms max=%lldms | hitch(>%dms)=%u "
                                  "| nflush=%u px/frame=%u | period indev=%ums refr=%ums -> %s",
                             (unsigned)s_prof_frames,
                             (long long)(avg_gap / 1000),  (long long)(s_prof_max_gap / 1000),
                             (long long)(avg_tmr / 1000),  (long long)(s_prof_max_tmr / 1000),
                             (long long)(avg_poll / 1000), (long long)(s_prof_max_poll / 1000),
                             (long long)(avg_fl / 1000),   (long long)(s_prof_max_fl / 1000),
                             LVGL_PROF_HITCH_MS, (unsigned)s_prof_hitch,
                             (unsigned)s_prof_flush, (unsigned)px_per_frame,
                             (unsigned)s_prof_indev_ms, (unsigned)s_prof_refr_ms,
                             (s_prof_max_self * 2 > s_prof_max_gap) ? "UI self work dominates"
                                                                    : "external preemption dominates");
                }
            }
        }
        /* 🔴 2026-10-01（修「task_wdt: - IDLE0 (CPU 0)」每 5 秒刷一条）：
         * 本工程 CONFIG_FREERTOS_HZ=100，而 pdMS_TO_TICKS 是**向下取整**
         * （(ms*HZ)/1000，见 projdefs.h）⇒ pdMS_TO_TICKS(2) == **0 tick**。
         * vTaskDelay(0) 只会 yield 一次，main 任务（prio 1、钉在 CPU0）随即又变
         * ready ⇒ 调度器永远选它、IDLE0（prio 0）**一次都跑不到** ⇒ CPU0 被 100%
         * 占满，task_wdt 每 5 秒报一次「IDLE0 did not reset the watchdog」，
         * 背栈恒落在 lv_timer_handler / sdgoods_lvgl_loop。
         * 修法：**至少让出 1 个 tick**（100 Hz 下 = 10 ms，屏刷新率上限 100 fps，
         * 对 360×360 SPI 屏绰绰有余），LVGL 心跳走 esp_timer(1 ms) 不受影响。
         *
         * 🔴 2026-10-02（「连着网没下载也卡」专项）**交互期改用 2ms 心跳唤醒**：
         * 上面那个 10ms 是**雷打不动的地板**，它把「本拍要渲染 14ms」的那一拍拉成
         * 24ms，于是 30ms 的输入/刷新周期只能被**量化到 30~38ms** —— 手感就是
         * "滑动一格一格蹦"。交互期改成「等 lvgl_tick_cb 的通知」，本拍周期变成
         * max(2ms, 本拍耗时)：没事做 ≈2ms 一拍（输入/刷新周期能准点执行），
         * 有帧要画 ≈本拍耗时（不再白搭 10ms）。
         * ⚠️ IDLE0 不会被饿死 —— 本任务在通知上**阻塞**，CPU0 有实实在在的空档；
         *    10ms 地板当初要解决的是 `vTaskDelay(0)` 的忙等，不是"让出得太少"。
         * ⚠️ 超时兜底 50ms：万一心跳停摆（时钟切换 / 定时器被删），循环也不会卡死。
         * ⚠️ **只在 s_ui_interactive 时走这条**（手指按住 + 松开后 400ms 内）；
         *    一松手立刻回到 10ms/拍的省电节奏，空闲功耗与改动前完全一致。 */
        if (s_ui_interactive) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        } else {
            const TickType_t lv_delay = pdMS_TO_TICKS(2);
            vTaskDelay(lv_delay > 0 ? lv_delay : 1);
        }
    }
}
