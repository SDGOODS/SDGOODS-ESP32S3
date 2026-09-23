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

static const char *TAG = "sdgoods_lvgl";
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static bool s_flush_async = true;   /* 双缓冲（PSRAM 全屏或内部 SRAM）时为 true；单块回退时 false */
static uint32_t s_flush_fail = 0;   /* 异步提交失败计数（弹跳预算不足时会持续增长，用于诊断） */

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
    /* PSRAM 缓冲走 CPU 缓存写：DMA 读取前必须把缓存行回写（clean）到物理 PSRAM，
     * 否则 DMA 会读到陈旧数据 -> 花屏/黑条。内部 SRAM 无缓存，无需此步。 */
#if CONFIG_SPIRAM && SOC_CACHE_WRITEBACK_SUPPORTED
    if (esp_ptr_external_ram(color_map)) {
        size_t bytes = (size_t)(area->x2 - area->x1 + 1) *
                       (size_t)(area->y2 - area->y1 + 1) * sizeof(lv_color_t);
        esp_cache_msync((void *)color_map, bytes,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
#endif
    /* 异步提交：仅把这块数据交给 DMA 就返回，完成后由 st77916.c 的
     * on_color_trans_done 回调调 lv_disp_flush_ready(drv) 释放缓冲。此刻 CPU 已去渲染
     * 另一块缓冲，渲染与传输重叠 -> 流畅无卡顿。全屏块时这就是「一帧一次」flush。 */
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
    if (s_flush_async) {
        lvgl_flush_cb_async(drv, area, color_map);
    } else {
        lvgl_flush_cb_sync(drv, area, color_map);
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
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
    while (1) {
        sdgoods_power_key_poll();
        /* 跨任务投递过来的闭包：**本工程唯一允许跨任务碰 LVGL 的入口**
         * （契约见 sdgoods_lvgl.h）。放在 lv_timer_handler() 之前 ⇒ 与 lv_async_call
         * 同为「下一拍生效」，但执行点不在 LVGL 的任何内部结构上。
         * 刻意放在 suspended 判断**之外**：熄屏（背光关、跳过渲染）期间投递的请求
         * 也要被执行掉，否则队列积压到满会被丢弃。 */
        lvgl_post_drain();
        /* 熄屏低功耗：主页短按电源键进入 suspended 后，跳过渲染与轮询，
         * 背光已关闭，仅保留电源键轮询以便下次短按唤醒。大幅省电且唤醒即时。 */
        if (!sdgoods_power_is_suspended()) {
            lv_timer_handler();
            /* 各应用的定时器推进（主页刷新 / 游戏物理 / 蓝牙收包 / 扫描结果…）
               由应用层汇总成一个函数注册进来，新增应用只需改 apps_registry.c，
               这个平台主循环永远不用动。 */
            sdgoods_apps_poll();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
