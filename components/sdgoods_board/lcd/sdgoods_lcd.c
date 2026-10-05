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

#include "sdgoods_lcd.h"
#include "lvgl.h"

#include <string.h>   /* memcpy：弹跳带逐带拷贝 */
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "soc/soc_caps.h"
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
#include "esp_cache.h"
#endif
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_rom_sys.h"        /* esp_rom_delay_us：TE 轮询的短延时 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "lcd";

esp_lcd_panel_handle_t panel_handle;
static SemaphoreHandle_t s_panel_mutex;
static SemaphoreHandle_t s_spi_color_done_sem;
static ledc_channel_config_t s_ledc_ch;
static bool s_backlight_ready;
static uint8_t s_backlight_pct;

/* 异步 LVGL flush 完成回调需要通知 LVGL 哪块已刷好；由 sdgoods_lvgl 在 drv 注册后赋值。
 * 注意：开机清屏(lcd_clear_black)调用 draw_bitmap_safe 时本指针尚为 NULL，
 * 故其完成只会给同步信号量、不会误触发 lv_disp_flush_ready。 */
static lv_disp_drv_t *s_lvgl_drv = NULL;

/* ---- 刷屏健康统计（2026-10-01「下载时花屏」排查探针）------------------
 * 背景：spi_master 为每片 4092B 分片在【内部 DMA 堆】上临时 malloc 一块弹跳缓冲
 * （S3 的 PSRAM 不在 SOC_DMA_LOW..HIGH 窗口内 ⇒ esp_ptr_dma_capable()==false
 *  ⇒ 必弹跳）。而下载/网络突发期内部堆会被 TLS/Wi-Fi 挤到只剩 ~20 KB，
 *  最大连续块随时可能跌破 4096 ⇒ 分片分配失败 ⇒ 该片画面被丢（花屏）。
 * 这里把「成功与否」之外的过程量都记下来：重试次数（NO_MEM 但 5ms 后拿到了）、
 * 完成信号量等待时长（被 flash 写/其他高优任务拖住的证据）、以及历次刷屏时
 * 看到的最小 dma_largest。`{dma!` 一次读走，不用全程刷日志。 */
static uint32_t s_st_retry;      /* NO_MEM 重试后成功的分片次数（≈「差点花屏」） */
static uint32_t s_st_fail;       /* 重试 12 次仍失败的整块次数（= 真花屏） */
static uint32_t s_st_slow_wait;  /* done 信号量等待 >50ms 的次数 */
static uint32_t s_st_stale;      /* 排队前排空到的「上一事务遗留信号」次数（竞态证据） */
static int64_t  s_st_worst_wait_us;
static size_t   s_st_min_dma_largest = SIZE_MAX;   /* 刷屏时见过的最小连续块 */
static int64_t  s_st_last_warn_us;                 /* NO_MEM 告警限流（250ms 一条） */

/* 单带完成的等待上限。🔴 2026-10-02：从 500ms 降到 200ms，且**排空改到排队之前**。
 *
 * 旧写法是「先 draw_bitmap，再 `xSemaphoreTake(sem,0)` 清残留，再等 500ms」。
 * 而本函数与 LVGL 的异步刷屏共用同一个 `s_spi_color_done_sem` ——
 * 一次带只有 ~0.2ms SPI 时间，**若本次传输在「draw_bitmap 返回后、清信号前」就完成了**
 * （下载期高优先级任务在跑时完全可能），那次**属于本次的真正完成信号会被这一句吞掉**，
 * 紧接着的等待就必然白等满 500ms。
 * 真机实测（下载期 `{dma!`）：`slow_wait=21 worst_wait=506ms` × 50s 下载，
 * 即 LVGL 线程被这些「必然超时」白卡了十几秒 —— 这是下载期卡顿的**直接**来源。
 * ⇒ 修法：① 信号量改**计数**式（每次完成都计数，不会被覆盖）；
 *          ② 排空放到 **draw_bitmap 之前**（那时不可能有本次的信号）；
 *          ③ 超时上限压到 200ms（正常一次带 <1ms，200ms 已是极宽裕的故障阈值）。 */
#define LCD_BAND_DONE_TIMEOUT_MS   200

/* ---- 自有内部 DMA 弹跳带（2026-10-01「下载时花屏」治本）----------------
 * 🔴 为什么必须：S3 的 PSRAM 不在 DMA 窗口（SOC_DMA_LOW..HIGH = 0x3FC88000..0x3FD00000）
 * ⇒ spi_master 对每个 4092B 分片都要【临时 malloc】一块内部 DMA 弹跳缓冲
 * （spi_master.c setup_priv_desc → heap_caps_aligned_alloc(MALLOC_CAP_DMA)）。
 * 下载期间 Wi-Fi 动态 TX/RX 缓冲 + TLS 在同一个堆上反复拿/还，实测把
 * 最大连续块打碎到 **2304B**（free 还有 13KB，但全是碎块）⇒ 每片都 NO_MEM
 * ⇒ 12 次重试仍失败 ⇒ 整帧被丢 = 用户看到的「下载时花屏」（真机 A/B 复现，
 * `{dma!`: retry=348 fail=29 min_dma_largest=2304）。
 * ✅ 治本：刷屏先把画面按「带」拷进**开机就占住、永不释放**的内部 DMA 缓冲，
 *    再从它发起 SPI —— 源在 DMA 窗口内 ⇒ spi_master 直接用，不再向堆要弹跳。
 *    刷屏从此与堆碎片完全无关。开销 = 一次 PSRAM→内部 memcpy（259KB 全屏 ~8ms）。
 * ⚠️ 尺寸 8KB 是权衡：太小（4KB）每屏分带次数翻倍；太大（16KB+）会把下载期
 *    本就只剩 ~13KB 的内部堆进一步挤爆（Wi-Fi 动态缓冲会拿不到）。
 *    若将来改这个值，必须连 `sdgoods_devlink.c` 下载期的 heap_int 数据一起看。
 *
 * 🚨 2026-10-02 试过并**证伪**的两条路（别再走）：
 *  ① **把一帧拆成多次 `esp_lcd_panel_io_tx_color` 连写**（「窗口只设一次，之后
 *     各带用 lcd_cmd = -1 续写同一段 RAMWR」）——看着能把每带的窗口开销省掉、
 *     保住事务池流水，实测**直接花屏**。根因：本工程 `dc_gpio_num = -1`（QSPI），
 *     **每次 CS 拉起都开启一个新的 QSPI 命令帧**，驱动内部的多分片续写是靠
 *     `SPI_TRANS_CS_KEEP_ACTIVE` 把 CS 一直拉低才成立的（见 IDF
 *     esp_lcd_panel_io_spi.c 的分片循环）。跨 tx_color 调用 CS 必然抖动 ⇒
 *     像素数据被当成命令字节喂给面板。⇒ **一次刷屏只能有一个 tx_color**。
 *  ② **靠缩小 LCD_SPI_MAX_TRANSFER_SIZE 把弹跳预算压下来**（老题，见
 *     sdgoods_lcd.h 的 2026-09-20 实测纠正）：有效分片恒为 4092B，缩宏无效。
 *  ⇒ 结论：分带路径**每带都必须自带完整窗口命令**（即逐带 draw_bitmap），
 *    因此它天然比「整帧一次 tx_color」多出「每带 drain + 3 条命令」的开销。
 *    既然如此，就只在**堆碎片化到直交路径会失败时**才用它 —— 见
 *    direct_path_ok() 的选路注释。 */
#define LCD_FLUSH_BAND_BYTES   8192
static uint8_t *s_band = NULL;
/* 🔴 2026-10-01 晚「下载慢 + 花屏」二次定案：**带尺寸必须能降级**。
 * 起因：A/B 调试把弹跳带 free 掉之后再也分配不回来 —— 下载期内部 DMA 的
 * **最大连续块只剩 3072~6144 B**（`{dma!` 实测 min_dma_largest=3072），
 * 而旧代码只认 8192 这一个尺寸 ⇒ 拿不到就静默退回老路 ⇒ 屏又花了。
 * 带的作用只是「让源地址落在 DMA 窗口内」，**尺寸大小不影响有效性**，
 * 只影响分带次数（带越小、一次刷屏的 SPI 调用次数越多，memcpy 总量不变）。
 * ⇒ 改成 8192 → 4096 → 2048 → 1024 逐级降级，**宁可多分几次带也不能裸奔**。
 * 拿到多大记在 s_band_bytes，供 `{dma!` 观察（`band=` 一行）。 */
static size_t s_band_bytes = 0;
static bool   s_band_alloc_tried = false;   /* 分配只试一次（见 band_alloc_locked） */
/* 运行时开关（调试键 `{band0!` / `{band1!`，见 launcher_main.c）。
 * 为什么要有：**下载吞吐 A/B** 必须能在同一台设备、同一条链路上来回切。
 * 2026-10-01 晚间用户报「下载好慢」，而弹跳带常驻占 8KB 内部 DMA ——
 * Wi-Fi 的动态 RX/TX 缓冲也在同一块堆上，两者是不是在抢，只能当场切换才测得出。
 * ⚠️ 关掉时**必须把缓冲 free 掉**才算还原旧布局（否则那 8KB 还占着，A/B 无意义）。
 * 关掉后刷屏回落到直接从 PSRAM 发（= 会花屏的老路），**只用于测速对照**。 */
static bool s_band_on = true;

void sdgoods_lcd_band_enable(bool on)
{
    if (on == s_band_on) {
        return;
    }
    if (s_panel_mutex && xSemaphoreTake(s_panel_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_band_on = on;
        if (on) {
            s_band_alloc_tried = false;   /* 重新打开时允许再试一次分配 */
        }
        if (!on && s_band) {
            heap_caps_free(s_band);
            s_band = NULL;
            s_band_bytes = 0;
        }
        xSemaphoreGive(s_panel_mutex);
    } else {
        s_band_on = on;      /* 拿不到锁：退而求其次只切标志（调试路径，够用） */
    }
    ESP_LOGW(TAG, "flush band %s (dma_largest=%u free=%u)",
             s_band_on ? "ON" : "OFF",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

size_t sdgoods_lcd_band_bytes(void)
{
    return s_band_bytes;      /* 0 = 还没分配到（或已被关掉），刷屏走在老路上 */
}

/* ---- TE（撕裂同步）门控 —— 2026-10-03 新增，治「切换壁纸有撕裂感」--------------
 *
 * 【为什么"写比一帧快"不等于不撕裂】
 *   面板按自己的时序连续扫 GRAM（360 行 @60Hz ⇒ 一帧 Tf = 16.7ms、扫一行 t_s = 46.3µs）；
 *   CPU 侧整屏写 GRAM 只需 259200 B ÷ 40MB/s（4-line @80MHz）≈ **Tw = 6.5ms**。
 *   但写入**不是原子的** —— 面板扫到哪一行，看到的就是那一刻 GRAM 里的值：
 *     设开始写时面板正扫到第 p 行，写一行耗时 t_w = Tw/360 ⇒ 第 k 行在 (k)·t_w 时刻
 *     被写入、在第 p 行之后 (k−p)·t_s 时刻被扫到。要"写赢"需要 (k)·t_w < (k−p)·t_s，
 *     即  k > p · Tf / (Tf − Tw)。⇒ **撕裂带恰好从 p 开始**，p 越接近 0 越干净。
 *   所以办法只有一个：**把 p 压到 0**，也就是"在帧的起点开始写"。而"帧的起点"
 *   只能问面板要 —— 那就是 TE 信号。
 *
 * 【本板的两件事实】
 *   ① `BOARD_LCD_SPI_IO_TE`（GPIO18）在 board_pins.h 里定义了，但**全工程零引用**
 *      ⇒ 撕裂同步从来没做过（grep 只有那一行定义）。
 *   ② 面板初始化序列（st77916_vendor_init.inc）里**没有 0x35**（TE Line ON）
 *      ⇒ 面板的 TE 引脚根本不输出。两条都得补上，缺一不可。
 *
 * 【实现】① 初始化末尾发 `0x35`（参数 0x00 = 仅 V-blank，一帧一个脉冲）打开 TE 输出；
 *         ② GPIO18 配成**输入 + 上拉**；③ 整屏级刷屏之前等一次电平跳变，再发像素。
 *
 * 【为什么只门控"整屏级"】等一次最长 1 帧。小区域刷新（时钟、圆点、状态条）每帧
 *   都发生，给它加一帧等于白送掉一半帧率；而它们只改一小块，撕裂本来也看不出来。
 *   整屏刷新是**偶发**的（切壁纸 / 进出极简 / 换页）—— 多等一帧无感知，
 *   却正好是用户能看见撕裂的那些场景。判据取 ≥80% 屏像素。
 *
 * 【失效自愈（必须有）】若 GPIO18 没接线、或面板不认 0x35 ⇒ 永远等不到跳变
 *   ⇒ 连续 4 次超时后**本次开机内永久关掉**门控。代价上限 = 4 × 25ms，一次性；
 *   绝不允许"每帧都等满超时"这种事。
 *   🔴 上拉也是为此：没接线时恒高 ⇒ 无跳变 ⇒ 干净地走自愈；悬空的话噪声会造成
 *      假跳变，门控会以为自己有效、于是**每帧随机多等一截**，比不加还糟。
 *
 * 【存疑待验】0x35 是否真被 ST77916 认、GPIO18 是否真接到面板的 TE —— 两条都
 *   只能靠实测（`{te!` 采 60ms：`min+max` 应 ≈16600µs）。
 *   ✅ 2026-10-03 首测（v2.0.61）已确认**信号存在且门控能锁上**：
 *      `edges=6 period=701µs level=0 | gate=ON dead=0 waits=4 timeouts=0 last=10687µs`
 *      —— 60ms 内 6 个边沿 ⇒ 一帧两个边沿 ⇒ 约 3.6 帧；`waits=4 timeouts=0`
 *      说明 4 次整屏刷屏都等到了跳变（不是超时兜底），最长一次等了 10.7ms
 *      （与"一帧内随机时刻等到下一个边沿"完全吻合）。
 *      ⚠️ 那一版的 `period` 报的是"最后一次相邻间隔"= 701µs（消隐脉冲宽），
 *      **不是**帧周期 —— 口径已改为 min/max（见 sdgoods_lcd_te_probe 的注释）。
 *   期望收益的口径也要说清：**撕裂在静帧截图里看不见**（截图读的是 LVGL 帧缓冲、
 *   不是面板回读）⇒ 判据只能是"TE 波形对得上 + 超时数为 0 + 相位余量够"，
 *   最终观感只能由人确认。 */
#define LCD_TE_AREA_MIN_PX      ((LCD_WIDTH * LCD_HEIGHT) * 4 / 5)  /* 整屏级：≥80% 像素 */
/* 等一次跳变的上限。取 40ms 而不是"一帧 16.7ms 加一点"：
 *   · 实测帧周期 16.45ms（60.7Hz）⇒ 正常最坏也就等 16.5ms；
 *   · 留出余量是为了两类**非故障**情形：轮询被高优先任务抢占了十几毫秒；
 *     以及万一某批面板跑 30Hz（帧 32ms）也不至于每次超时。
 *   代价：真"没接"时最多 4×40=160ms（一次性），可忽略。 */
#define LCD_TE_WAIT_TIMEOUT_US  40000
#define LCD_TE_POLL_US          8          /* 轮询步长 ⇒ 对齐误差 ≤8µs（一行的 0.02%） */
#define LCD_TE_DEAD_STRIKES     4          /* 连续超时几次就判"没接" */
#define LCD_TE_STATIC_GAP_US    150000     /* 两次整屏刷新隔多久才算"静止"（见 ② 判据） */

static bool     s_te_gate_on    = true;   /* 运行时开关 `{te0!` / `{te1!` */
static bool     s_te_gpio_ready = false;
static bool     s_te_dead       = false;  /* 判"没接" ⇒ 本次开机内不再等 */
static int64_t  s_te_last_big_us = 0;     /* 上一次整屏级刷屏的时刻（"静止"判据） */
static uint32_t s_te_waits;               /* 对齐尝试次数 */
static uint32_t s_te_timeouts;            /* 其中超时的次数 */
static uint32_t s_te_starved;             /* 其中"被抢占"而非"没信号"的次数 */
static uint32_t s_te_skipped_motion;      /* 因"上一帧刚整屏刷过"而跳过对齐的次数 */
static uint32_t s_te_strikes;             /* 连续超时计数 */
static uint32_t s_te_period_us;           /* 最近一次等到的间隔 */
static int      s_te_level = -1;          /* 上次等之前看到的电平 */

static void te_gpio_init(void)
{
    if (s_te_gpio_ready) {
        return;
    }
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << BOARD_LCD_SPI_IO_TE,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    s_te_gpio_ready = (gpio_config(&c) == ESP_OK);
    ESP_LOGI(TAG, "TE pin %d configured as input (pull-up): %s",
             BOARD_LCD_SPI_IO_TE, s_te_gpio_ready ? "ok" : "FAILED");
}

/* 等 TE 电平跳变 = 等到一帧的边界（升/降沿都算：不赌极性）。
 * 返回 false = 超时。`*starved` 会置位表示"这次超时**不能**归因于面板没信号"
 *   —— 见下面那段（2026-10-03 实测结论）。
 *
 * 🔴 **为什么必须区分"没信号"与"被抢占"**：本循环是**忙等**，但任务仍会被更高
 *   优先级的活（devlink 的 TLS 上行 / Wi-Fi / LVGL）抢占。实测（`{te!` 连测 6 次，
 *   负载下）有一次采到 `min=282µs`（比真实消隐脉冲 690µs 还短 ⇒ 必然是**晚采到**
 *   了脉冲前沿）+ `max=49031µs`（漏采了两帧）—— 即轮询被饿死 ~49ms。
 *   若把这种超时也记成"面板没信号"，连续 4 次就会把门控**永久关掉**（本次开机），
 *   而实际上 TE 好好的。⇒ 只有"轮询本身走得正常、却始终没有跳变"才算没信号。 */
#define LCD_TE_STARVE_GAP_US   3000   /* 单次轮询间隔 >3ms 就算被抢占过（正常 ~8µs） */

static bool te_wait_edge(uint32_t timeout_us, bool *starved)
{
    const gpio_num_t pin = (gpio_num_t)BOARD_LCD_SPI_IO_TE;
    const int l0 = gpio_get_level(pin);
    *starved = false;
    s_te_level = l0;
    const int64_t t0 = esp_timer_get_time();
    int64_t prev = t0;
    for (;;) {
        if (gpio_get_level(pin) != l0) {
            s_te_period_us = (uint32_t)(esp_timer_get_time() - t0);
            s_te_strikes = 0;
            return true;
        }
        const int64_t now = esp_timer_get_time();
        if (now - prev > LCD_TE_STARVE_GAP_US) {
            *starved = true;
        }
        prev = now;
        if (now - t0 > (int64_t)timeout_us) {
            return false;
        }
        esp_rom_delay_us(LCD_TE_POLL_US);
    }
}

/* 整屏级刷屏前的 TE 对齐（在**取完锁、真正发像素之前**调）。
 *
 * 三重门控，缺一不可：
 *   ① 面积 ≥80% 屏：小区域（时钟 / 圆点 / 状态条）撕裂看不出来，且它们每帧都刷，
 *      给它加一帧延迟等于白丢一半帧率。
 *   ② **上一帧整屏刷新要隔得够久**（"静止"判据）：连续整屏刷新 = 拖动 / 动画，
 *      此时撕裂被运动本身掩盖（人眼对运动中的错位不敏感），而给连续帧逐帧加
 *      平均 ~8ms 的等待会实打实地掉帧。⇒ 只在"静了 → 突然整屏换一张"时对齐，
 *      那正是用户能看见撕裂的场景（切壁纸 / 进出极简的落定那一帧）。
 *   ③ 门控开着且没被判"没接"。
 * ⚠️ s_te_last_big_us 必须在①②之后、门控开关之前更新：否则关掉再打开时，
 *    会把"上一帧其实刚刷过"误判成静止。 */
static void te_align_if_big(int x1, int y1, int x2, int y2)
{
    if ((int64_t)(x2 - x1) * (int64_t)(y2 - y1) < (int64_t)LCD_TE_AREA_MIN_PX) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    const bool isolated = (s_te_last_big_us == 0) ||
                          (now - s_te_last_big_us > (int64_t)LCD_TE_STATIC_GAP_US);
    s_te_last_big_us = now;

    if (!s_te_gate_on || s_te_dead) {
        return;
    }
    if (!isolated) {
        s_te_skipped_motion++;
        return;
    }
    te_gpio_init();
    if (!s_te_gpio_ready) {
        s_te_dead = true;
        return;
    }
    s_te_waits++;
    bool starved = false;
    if (!te_wait_edge(LCD_TE_WAIT_TIMEOUT_US, &starved)) {
        s_te_timeouts++;
        if (starved) {
            /* 被抢占导致的超时：**不算**"面板没信号"，不累计 strike（见 te_wait_edge 上方）。
             * 此时不放行地把画面写出去 —— 相位没对上、撕裂这一帧照旧，但比误关门控强。 */
            s_te_starved++;
            ESP_LOGW(TAG, "TE wait timed out but poll was preempted -> not counting as dead");
            return;
        }
        if (++s_te_strikes >= LCD_TE_DEAD_STRIKES) {
            s_te_dead = true;
            ESP_LOGW(TAG, "TE: %u consecutive waits timed out on pin %d -> gate DISABLED "
                          "(TE not wired, or 0x35 not honoured by this panel)",
                     (unsigned)s_te_timeouts, BOARD_LCD_SPI_IO_TE);
        }
    }
}

void sdgoods_lcd_te_enable(bool on)
{
    s_te_gate_on = on;
    if (on) {
        s_te_dead = false;      /* 手动重开 = 清掉自愈结论，允许再试 */
        s_te_strikes = 0;
    }
    te_gpio_init();
    ESP_LOGW(TAG, "TE gate %s (pin=%d lvl=%d)",
             on ? "ON" : "OFF", BOARD_LCD_SPI_IO_TE,
             s_te_gpio_ready ? gpio_get_level((gpio_num_t)BOARD_LCD_SPI_IO_TE) : -1);
}

/* 主动采一段时间的 TE 波形（`{te!` 用）。
 *
 * 🔴 口径必须是**最小/最大相邻间隔**，不能只报"最后一次间隔"：
 *    模式 1（仅 V-blank）的 TE 是**一帧两个边沿** —— 一个宽 ~0.7ms 的脉冲
 *    （消隐期）+ 一段 ~16ms 的空档（有效扫描期）。相邻间隔就在这两个值之间交替。
 *    ⇒ 真正的**帧周期 = min + max**（这也正是 `te_wait_edge` 等一次跳变
 *      平均要 ~8ms、最长 ~16ms 的原因）。只报最后一个间隔会得到 700µs 这种
 *      "看起来不像 60Hz"的数，把人带偏（2026-10-03 第一版就踩了这个）。
 *
 * 🔴 `poll_max_us` 是**这次采样可不可信**的判据，必须一起看：
 *    本函数同样是忙等轮询，会被更高优先级的活（devlink 的 TLS 上行 / Wi-Fi / LVGL）
 *    抢占。实测（2026-10-03，负载下连测 6 次）有一次得到
 *    `edges=4 min=282us max=49031us` —— `min=282µs` **比真实消隐脉冲 690µs 还短**，
 *    只可能是"晚采到了脉冲前沿"，`max=49031µs` 则是漏采了两帧。
 *    ⇒ 那一条是**被饿死的伪影**，不是面板掉到 30Hz。
 *    判据：`poll_max_us` 明显大于 ~10µs（正常轮询步长）⇒ min/max 不可信、重测。
 *    其余 5 次都是 `min≈690µs max≈15756µs frame≈16445µs`（60.7Hz）。 */
void sdgoods_lcd_te_probe(uint32_t ms, uint32_t *edges, uint32_t *min_us, uint32_t *max_us,
                          uint32_t *poll_max_us, int *level)
{
    te_gpio_init();
    const gpio_num_t pin = (gpio_num_t)BOARD_LCD_SPI_IO_TE;
    int      lvl = s_te_gpio_ready ? gpio_get_level(pin) : -1;
    uint32_t e = 0, mn = 0, mx = 0, pmax = 0;
    int32_t  last = -1;
    const int64_t t0 = esp_timer_get_time();
    int64_t prev = t0;
    while ((esp_timer_get_time() - t0) < (int64_t)ms * 1000) {
        const int v = gpio_get_level(pin);
        if (v != lvl) {
            lvl = v;
            const int32_t tr = (int32_t)(esp_timer_get_time() - t0);
            if (last >= 0) {
                const uint32_t iv = (uint32_t)(tr - last);
                if (mn == 0 || iv < mn) mn = iv;
                if (iv > mx)            mx = iv;
            }
            last = tr;
            e++;
        }
        const int64_t now = esp_timer_get_time();
        if ((uint32_t)(now - prev) > pmax) {
            pmax = (uint32_t)(now - prev);   /* 本次轮询间隔的最大值 = 被抢占的证据 */
        }
        prev = now;
        esp_rom_delay_us(10);
    }
    if (edges)       *edges       = e;
    if (min_us)      *min_us      = mn;
    if (max_us)      *max_us      = mx;
    if (poll_max_us) *poll_max_us = pmax;
    if (level)       *level       = lvl;
}

void sdgoods_lcd_te_stats(bool *gate_on, bool *dead, uint32_t *waits, uint32_t *timeouts,
                          uint32_t *starved, uint32_t *skipped_motion, uint32_t *period_us,
                          int *level)
{
    if (gate_on)        *gate_on        = s_te_gate_on;
    if (dead)           *dead           = s_te_dead;
    if (waits)          *waits          = s_te_waits;
    if (timeouts)       *timeouts       = s_te_timeouts;
    if (starved)        *starved        = s_te_starved;
    if (skipped_motion) *skipped_motion = s_te_skipped_motion;
    if (period_us)      *period_us      = s_te_period_us;
    if (level)          *level          = s_te_level;
}

/* 单带发送（含 NO_MEM 重试 + done 信号量等待 + 统计）。调用方必须已持 s_panel_mutex。 */
static esp_err_t draw_one_locked(int x1, int y1, int x2, int y2, const void *color_data)
{
    esp_err_t err = ESP_FAIL;

    /* 🔴 排空必须在**排队之前**（理由见 LCD_BAND_DONE_TIMEOUT_MS 上方的长注释）：
     * 排队之后清信号会吞掉「本次」刚到的完成信号 ⇒ 后面的等待必然白等满超时。 */
    if (s_spi_color_done_sem) {
        while (xSemaphoreTake(s_spi_color_done_sem, 0) == pdTRUE) {
            s_st_stale++;
        }
    }

    for (int i = 0; i < 12; i++) {
        err = esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, color_data);
        if (err == ESP_OK) {
            /* 同步等待本次传输真正完成（on_color_trans_done 仅在最后一个分片完成时给信号量）。
             * 关键：保证任意时刻只有“本事务”在飞，DMA 描述符池不会被整屏连续刷新的
             * 多个事务同时占满 -> 否则 setup_priv_desc 分配描述符失败返回 NO_MEM，
             * 整块区域被丢弃，表现为固定的水平黑带。 */
            if (s_spi_color_done_sem) {
                const int64_t tw0 = esp_timer_get_time();
                if (xSemaphoreTake(s_spi_color_done_sem,
                                   pdMS_TO_TICKS(LCD_BAND_DONE_TIMEOUT_MS)) != pdTRUE) {
                    ESP_LOGW(TAG, "draw_bitmap done sem timeout x:%d y:%d", x1, y1);
                }
                const int64_t tw = esp_timer_get_time() - tw0;
                if (tw > s_st_worst_wait_us) {
                    s_st_worst_wait_us = tw;
                }
                if (tw > 50000) {
                    s_st_slow_wait++;
                }
            }
            break;
        }
        if (err == ESP_ERR_NO_MEM) {
            /* 描述符池/队列暂满：让出 CPU 让 SPI DMA 把已排队事务跑完、腾出资源，再重试 */
            s_st_retry++;
            const int64_t now = esp_timer_get_time();
            if (now - s_st_last_warn_us > 250000) {   /* 限流：花屏高发期每 250ms 至多一条 */
                s_st_last_warn_us = now;
                ESP_LOGW(TAG, "draw_bitmap NO_MEM retry (bounce alloc tight) dma_largest=%u free=%u",
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ESP_LOGW(TAG, "draw_bitmap retry %d x:%d y:%d w:%d h:%d err=0x%x",
                 i, x1, y1, x2 - x1, y2 - y1, err);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return err;
}

void sdgoods_lcd_flush_stats(uint32_t *retry, uint32_t *fail, uint32_t *slow_wait,
                             uint32_t *worst_wait_ms, size_t *min_dma_largest)
{
    if (retry)           *retry           = s_st_retry;
    if (fail)            *fail            = s_st_fail;
    if (slow_wait)       *slow_wait       = s_st_slow_wait;
    if (worst_wait_ms)   *worst_wait_ms   = (uint32_t)(s_st_worst_wait_us / 1000);
    if (min_dma_largest) *min_dma_largest = (s_st_min_dma_largest == SIZE_MAX) ? 0
                                                                          : s_st_min_dma_largest;
}

/* 「排队前排空到的遗留完成信号」次数（2026-10-02 加的竞态取证量）。
 * 判据：它与 slow_wait 一起看 —— 若曾出现「清到的信号其实是本次的」，说明旧写法
 * （排队后才清）确实在吞信号；改成排队前排空后此值应稳定为 0。 */
uint32_t sdgoods_lcd_stale_tokens(void)
{
    return s_st_stale;
}

void sdgoods_lcd_set_lvgl_drv(lv_disp_drv_t *drv)
{
    s_lvgl_drv = drv;
}
#include "st77916_vendor_init.inc"

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t hp = pdFALSE;
    /* 异步 LVGL flush：本块 DMA 完成，通知 LVGL 释放该缓冲给下次渲染
     * （此刻 CPU 已并行渲染另一块，渲染与传输重叠 -> 无串行卡顿）。 */
    if (s_lvgl_drv) {
        lv_disp_flush_ready(s_lvgl_drv);
        hp = pdTRUE;
    }
    /* 同步调用方（如开机清屏）等待完成信号 */
    if (s_spi_color_done_sem) {
        xSemaphoreGiveFromISR(s_spi_color_done_sem, &hp);
    }
    return hp == pdTRUE;
}

void sdgoods_lcd_apply_vendor_madctl(void)
{
    if (!panel_handle) {
        return;
    }
    esp_err_t err = esp_lcd_panel_st77916_set_madctl(panel_handle, BOARD_ST77916_VENDOR_MADCTL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MADCTL 0x%02x: %s", BOARD_ST77916_VENDOR_MADCTL, esp_err_to_name(err));
    }
}

/* 弹跳带分配（逐级降级）。调用方必须已持 s_panel_mutex。
 * 只在开机第一次刷屏时试一次：失败就不必每帧重试（否则既浪费又刷屏）。 */
static void band_alloc_locked(void)
{
    if (s_band || s_band_alloc_tried) {
        return;
    }
    s_band_alloc_tried = true;
    /* 逐级降级（理由见 s_band_bytes 处的留档）：下载期连续块可能只剩 3~6KB，
     * 只认 8192 会拿不到而退回老路（= 花屏）。 */
    static const size_t k_try[] = { LCD_FLUSH_BAND_BYTES, 4096, 2048, 1024 };
    for (size_t t = 0; t < sizeof(k_try) / sizeof(k_try[0]); t++) {
        s_band = heap_caps_aligned_alloc(32, k_try[t], MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_band) {
            s_band_bytes = k_try[t];
            if (k_try[t] != LCD_FLUSH_BAND_BYTES) {
                ESP_LOGW(TAG, "flush band degraded: %u -> %u B (dma_largest=%u)",
                         (unsigned)LCD_FLUSH_BAND_BYTES, (unsigned)k_try[t],
                         (unsigned)heap_caps_get_largest_free_block(
                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
            }
            return;
        }
    }
    ESP_LOGW(TAG, "flush band alloc failed (down to 1024) -> fallback to direct draw");
}

/* 🔴 2026-10-02（下载期卡顿第四轮定案）：**按堆健康度选路**。
 *
 * 【背景】刷屏有两条路：
 *   · 直交路径：整帧一次 `esp_lcd_panel_draw_bitmap(PSRAM 源)` —— 面板 IO 的分片
 *     循环会用 2 深事务池把「弹跳 malloc + memcpy」与「SPI 传输」流水起来，
 *     **每帧开销最小**（空闲期实测 jank 探针 gap 中位 50ms）。
 *     代价：PSRAM 不在 DMA 窗口 ⇒ 驱动要为每个在途分片（有效分片恒 4092B，
 *     见 sdgoods_lcd.h）临时 malloc 一块 ~4096B **内部连续** DMA 弹跳。
 *   · 分带路径：把画面拷进预留的弹跳带再发（见上方 LCD_FLUSH_BAND_BYTES）。
 *     与堆碎片**完全无关**，但每带都要自带窗口命令 ⇒ 每带多一次 drain + 3 条命令，
 *     空闲期实测 gap 中位 74ms，比直交路径差一档（① 那段注释是同一天的证伪记录）。
 *
 * 【判据】直交路径的硬需求 = `队列深度 2 × 4096B`（见 sdgoods_lcd.h 的预算表）
 * ⇒ 内部 DMA 最大**连续**块 ≥ 8192B 时才选它。空闲期实测 largest=31744（稳过），
 * 下载期 Wi-Fi 动态缓冲会把 largest 打碎到 2688~3968（必挂）—— 判据刚好把两者分开。
 * 用 largest（最大连续块）而不是 free（总空闲）至关重要：下载期正是
 * 「free 还有 13KB 但全是碎块」。
 *
 * 【回退】直交路径万一仍失败（判据之后堆又被别的任务拿走），
 * draw_one_locked 内部会重试 12 次；仍失败且弹跳带已在手时，用分带路径兜一帧。 */
static bool direct_path_ok(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
           >= (size_t)(LCD_SPI_TRANS_QUEUE_SZ * 4096);
}

/* 逐带 draw_bitmap：每带自带完整窗口命令（**一次刷屏只能一个 tx_color 的例外**，
 * 理由见 LCD_FLUSH_BAND_BYTES 上方 ① 处）。调用方必须已持 s_panel_mutex 且 s_band 非空。 */
static esp_err_t flush_bands_locked(int x1, int y1, int x2, int y2, const void *src)
{
    const size_t row_bytes = (size_t)(x2 - x1) * 2u;
    int rows_per_band = (int)(s_band_bytes / row_bytes);
    if (rows_per_band < 1) {
        rows_per_band = 1;   /* 防御：带小到装不下一行时退化为逐行 */
    }
    esp_err_t err = ESP_OK;
    int y = y1;
    while (y < y2) {
        int rows = y2 - y;
        if (rows > rows_per_band) {
            rows = rows_per_band;
        }
        memcpy(s_band, (const uint8_t *)src + (size_t)(y - y1) * row_bytes,
               (size_t)rows * row_bytes);
        err = draw_one_locked(x1, y, x2, y + rows, s_band);
        if (err != ESP_OK) {
            break;
        }
        y += rows;
    }
    return err;
}



esp_err_t sdgoods_lcd_draw_bitmap_safe(int x1, int y1, int x2, int y2, const void *color_data)
{
    if (!panel_handle || !color_data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_panel_mutex) {
        s_panel_mutex = xSemaphoreCreateMutex();
    }
    if (!s_spi_color_done_sem) {
        s_spi_color_done_sem = xSemaphoreCreateCounting(8, 0);   /* 计数式，理由见上面那处 */
    }
    if (xSemaphoreTake(s_panel_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 采样点：每次真实刷屏都记一次当时的最大连续块（ silent，只在变小时更新） */
    {
        const size_t dl = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (dl < s_st_min_dma_largest) {
            s_st_min_dma_largest = dl;
        }
    }

    esp_err_t err;
    if (esp_ptr_external_ram(color_data)) {
        /* PSRAM 源：两条路选一条，判据见 direct_path_ok()。
         * 弹跳带仍**开机第一次刷屏就占住**（那时堆最健康；也避免下载期再去分配），
         * 但它只在堆碎片化到直交路径会失败时才真正上阵。 */
        if (s_band_on) {
            band_alloc_locked();
        }
        const bool use_band = (s_band_on && s_band && !direct_path_ok());
        if (!use_band) {
            /* 直交路径：整帧一次 tx_color（事务池 2 深流水，每帧开销最小）。
             * ⚠️ CPU 刚写过的脏行还在 cache 里，SPI DMA 从 PSRAM 直接读会读到
             *    旧内容 ⇒ 必须先 C2M 回写。
             * 🔴 msync 必须排在 **te_align_if_big() 之前**：它要 ~2.5ms（259200B），
             *    放在等完 TE 之后就把相位 p 推后 ~55 行 ⇒ 屏幕顶部露出撕裂带。
             *    见 te_align_if_big() 上方那段相位推算。 */
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
            const size_t bytes = (size_t)(x2 - x1) * (size_t)(y2 - y1) * sizeof(uint16_t);
            (void)esp_cache_msync((void *)color_data, bytes,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
#endif
        }
        /* TE 对齐：紧贴"真正发第一个像素"之前（小区域会被内部挡掉）。 */
        te_align_if_big(x1, y1, x2, y2);
        if (use_band) {
            err = flush_bands_locked(x1, y1, x2, y2, color_data);
        } else {
            err = draw_one_locked(x1, y1, x2, y2, color_data);
            /* 判据判完之后堆又被别的任务拿走（下载期可能）：有带就分带重发兜这一帧，
             * 别让整帧被丢（花屏优于黑屏）。 */
            if (err != ESP_OK && s_band_on && s_band) {
                ESP_LOGW(TAG, "direct path failed on fragmented heap -> band fallback");
                err = flush_bands_locked(x1, y1, x2, y2, color_data);
            }
        }
    } else {
        /* 内部 RAM 源（回退绘制缓冲模式）：本就在 DMA 窗口内、不需要 msync，
         * 直接在发之前对齐。 */
        te_align_if_big(x1, y1, x2, y2);
        err = draw_one_locked(x1, y1, x2, y2, color_data);
    }
    if (err != ESP_OK) {
        s_st_fail++;
        ESP_LOGE(TAG, "draw_bitmap FAILED x:%d y:%d w:%d h:%d err=0x%x ext=%d bytes=%d",
                 x1, y1, x2 - x1, y2 - y1, err,
                 (int)esp_ptr_external_ram(color_data),
                 (int)((size_t)(x2 - x1) * (size_t)(y2 - y1) * 2));
        /* 诊断：弹跳缓冲分配失败时报告内部 DMA 堆的真实余量，用来确定
         * LCD_SPI_MAX_TRANSFER_SIZE × LCD_SPI_TRANS_QUEUE_SZ 该压到多少。
         * 判据：largest_free 必须大于分片字节数，否则每一片都必然失败
         * （只降队列深度是没用的 —— 在途事务少不等于单块能拿到）。 */
        ESP_LOGE(TAG, "  internal DMA largest_free=%u bytes free=%u bytes (chunk=%d x queue=%d)",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                 (int)LCD_SPI_MAX_TRANSFER_SIZE, (int)LCD_SPI_TRANS_QUEUE_SZ);
    }
    xSemaphoreGive(s_panel_mutex);
    return err;
}

static void backlight_init(void)
{
    if (s_backlight_ready) {
        return;
    }
    ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LS_MODE,
        .timer_num = LEDC_HS_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    s_ledc_ch.gpio_num = BOARD_LCD_GPIO_BACKLIGHT;
    s_ledc_ch.speed_mode = LEDC_LS_MODE;
    s_ledc_ch.channel = LEDC_HS_CH0_CHANNEL;
    s_ledc_ch.intr_type = LEDC_INTR_DISABLE;
    s_ledc_ch.timer_sel = LEDC_HS_TIMER;
    s_ledc_ch.duty = 0;
    s_ledc_ch.hpoint = 0;
#if defined(BOARD_LCD_BACKLIGHT_LEDC_INVERT) && BOARD_LCD_BACKLIGHT_LEDC_INVERT
    s_ledc_ch.flags.output_invert = 1;
#endif
    ESP_ERROR_CHECK(ledc_channel_config(&s_ledc_ch));
    s_backlight_ready = true;
}

void sdgoods_lcd_set_backlight(uint8_t light)
{
    if (!s_backlight_ready) {
        backlight_init();
    }
    if (light > 100) {
        light = 100;
    }
    s_backlight_pct = light;
    uint32_t duty = (LEDC_MAX_Duty * light) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(s_ledc_ch.speed_mode, s_ledc_ch.channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(s_ledc_ch.speed_mode, s_ledc_ch.channel));
}

uint8_t sdgoods_lcd_get_backlight(void)
{
    return s_backlight_pct;
}

static void lcd_pow_on(void)
{
#if BOARD_LCD_POW_EN_GPIO >= 0
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_POW_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BOARD_LCD_POW_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
#endif
}

static void lcd_hw_reset(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_GPIO_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BOARD_LCD_GPIO_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_LCD_GPIO_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static int lcd_qspi_bus_init(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    const int pins[] = {
        BOARD_LCD_SPI_IO_SCK, BOARD_LCD_SPI_IO_DATA0, BOARD_LCD_SPI_IO_DATA1,
        BOARD_LCD_SPI_IO_DATA2, BOARD_LCD_SPI_IO_DATA3, BOARD_LCD_SPI_IO_CS,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (pins[i] >= 0) {
            gpio_reset_pin((gpio_num_t)pins[i]);
        }
    }
#endif

    spi_bus_config_t bus = {
        .data0_io_num = BOARD_LCD_SPI_IO_DATA0,
        .data1_io_num = BOARD_LCD_SPI_IO_DATA1,
        .sclk_io_num = BOARD_LCD_SPI_IO_SCK,
        .data2_io_num = BOARD_LCD_SPI_IO_DATA2,
        .data3_io_num = BOARD_LCD_SPI_IO_DATA3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = LCD_SPI_MAX_TRANSFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    if (spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return 0;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_SPI_IO_CS,
        .dc_gpio_num = -1,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = BOARD_LCD_SPI_PCLK_HZ,
        .trans_queue_depth = LCD_SPI_TRANS_QUEUE_SZ,
        .lcd_cmd_bits = LCD_SPI_CMD_BITS,
        .lcd_param_bits = LCD_SPI_PARAM_BITS,
        .flags = {
            .quad_mode = 1,
        },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io) != ESP_OK) {
        ESP_LOGE(TAG, "panel_io failed");
        return 0;
    }

    if (!s_spi_color_done_sem) {
        /* 🔴 **计数**信号量而不是二值（2026-10-02 改）：二值信号量下，
         * 「上一次完成」与「本次完成」会互相覆盖，让路闸门排空/等待就会错位
         * （详见 LCD_BAND_DONE_TIMEOUT_MS 上方注释）。计数上限 8 足够：
         * 一帧最多 33 个带，但每带都会被同步消费掉。 */
        s_spi_color_done_sem = xSemaphoreCreateCounting(8, 0);
    }
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_done };
    (void)esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL);

    st77916_vendor_config_t vendor = {
        .init_cmds = vendor_specific_init_vendor,
        .init_cmds_size = sizeof(vendor_specific_init_vendor) / sizeof(vendor_specific_init_vendor[0]),
        .flags = {
            .use_qspi_interface = 1,
            .init_madctl_valid = 1,
        },
        .init_madctl = BOARD_ST77916_VENDOR_MADCTL,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_GPIO_RST,
        .rgb_ele_order = BOARD_LCD_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_COLOR_BITS,
        .vendor_config = &vendor,
    };
    if (esp_lcd_new_panel_st77916(io, &panel_cfg, &panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "new_panel_st77916 failed");
        return 0;
    }

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    sdgoods_lcd_apply_vendor_madctl();
    esp_lcd_panel_disp_on_off(panel_handle, true);
    return 1;
}

static void lcd_clear_black(void)
{
    const size_t pixels = (size_t)LCD_WIDTH * LCD_HEIGHT;
    uint16_t *buf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < pixels; i++) {
        buf[i] = 0;
    }
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
    (void)esp_cache_msync(buf, pixels * sizeof(uint16_t),
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
#endif
    (void)sdgoods_lcd_draw_bitmap_safe(0, 0, LCD_WIDTH, LCD_HEIGHT, buf);
    heap_caps_free(buf);
}

void sdgoods_lcd_init_panel(void)
{
    lcd_pow_on();
    lcd_hw_reset();
    if (!lcd_qspi_bus_init()) {
        ESP_LOGE(TAG, "LCD init failed");
        return;
    }
    lcd_clear_black();
    backlight_init();
    sdgoods_lcd_set_backlight(0);
}
