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
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#define LCD_WIDTH       360
#define LCD_HEIGHT      360
#define LCD_COLOR_BITS  16

#define LCD_SPI_HOST               SPI2_HOST
#define LCD_SPI_MODE               0
/* 事务队列深度 = 2（⚠️ 与【全屏 PSRAM 绘制缓冲】强绑定，勿随意加大）。
 * 硬约束：PSRAM 不在 DMA 窗口内（esp_ptr_dma_capable 只认内部 SRAM 窗口），spi_master 的
 * setup_priv_desc 会为【每个在途事务】malloc 一块内部 DMA 弹跳缓冲，分配失败直接返回
 * ESP_ERR_NO_MEM(0x101)；而 esp_lcd_panel_io_spi 的分片循环一遇 queue 失败即刻 break
 * -> 画面残缺/黑带。
 * ⚠️ 弹跳缓冲大小**不等于** LCD_SPI_MAX_TRANSFER_SIZE（见下方 2026-09-20 实测纠正）：
 *   总线把 max_transfer_sz 向上取整到 DMA 描述符粒度（ESP32-S3 = 4092B，最少 1 个描述符），
 *   所以有效分片恒为 **4092B**，弹跳缓冲按 64B 对齐后按 **~4096B** 分配。
 *   真实预算 = 队列深度 × 4096 必须 < 内部 DMA 堆的 largest_free：
 *     队列 2 ⇒ 需 ≥ 8192B    队列 1 ⇒ 需 ≥ 4096B
 * 取 2 是为了流水掩盖「弹跳分配 + memcpy」间隙。
 *   2026-09-29 更正（**启动器已不再"余量充足"，别再按旧结论判断**）：
 *     · 启动器原先确实有 ~31KB，但 **P1 给启动器加了 WiFi + HTTP（sdgoods_devlink）
 *       之后内部 DMA 掉到 `largest_free=3968B`** —— 低于分片所需的 4096B
 *       ⇒ 每一片都失败、整帧被丢。用户可见症状 = **开机看不到主页**
 *       （`shot: init` 之后第一帧整屏 flush 就失败，面板停在上一帧/黑屏）。
 *     · 修法与 app 侧同源：把启动器的 WiFi 缓冲档位也压到最低档
 *       （`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=3` / `DYNAMIC_RX_BUFFER_NUM=16` /
 *        `DYNAMIC_TX_BUFFER_NUM=16`，见工程根 `sdkconfig` 与 `sdkconfig.defaults` 尾注）。
 *       ⚠️ 这是**启动器与 app 都要各自配一遍**的事，抄 BSP 源码不会带过去。
 *     · 排查顺序固定：先看 `internal DMA largest_free` 是否 ≥ 8192（队列 2），
 *       不够就先查 WiFi 缓冲档位，再考虑把队列降到 1（需求减半到 4096B）。
 *   EBADGE app 侧靠 2026-09-20 的「回收 WiFi 静态 RX 缓冲」把余量从 1664B 提到 ~13KB。
 * （曾取 40 => 需 160KB，每帧必 0x101；叠加旧版「flush 失败不调 lv_disp_flush_ready」，
 *   就表现为「CC 点音量/亮度进二级页 → 死机」+ 主页横滑「断成 3 段」的撕裂。）
 * 事务池由 calloc 分配（内部 SRAM），2 档开销可忽略。 */
#define LCD_SPI_TRANS_QUEUE_SZ     2
#define LCD_SPI_CMD_BITS           32
#define LCD_SPI_PARAM_BITS         8
/* 单次 SPI 事务**请求**最大字节数 = 512。
 * ⚠️ 本值**不是**最终分片大小，别再拿它当弹跳缓冲大小去推预算（2026-09-20 实测纠正）：
 *   spi_bus_initialize -> spicommon_dma_desc_alloc():
 *     dma_desc_ct    = ceil(cfg_max_sz / DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED)  // 最少 1
 *     *actual_max_sz = dma_desc_ct * 4092
 *   即 max_transfer_sz 被**向上取整**到 4092 的整数倍，且最少一个描述符 ——
 *   512 / 1024 / 2048 / 4092 配出来的有效分片**全都是 4092B**。
 *   esp_lcd_panel_io_spi 用 spi_bus_get_max_transaction_len()（实测打印 = 4092）
 *   当分片上限，弹跳缓冲实际按 ~4096B 分配。
 *   本宏只有在 > 4092 时才会把分片放大（如 8192 ⇒ 8184），故取小值最省描述符内存。
 *   判据仍看 `largest_free`（最大**连续**块），不是总空闲量。
 *
 * 2026-09-19 真机事故（两轮定位，别改回去）：
 *   · app（SDGOODS_EBADGE）里内部 DMA 本来就紧：LVGL init 时最大连续块
 *     只有 **9728B**（启动器同点位有 31744B）。
 *   · 唤出控制中心要整屏重绘（360×360×2 = 259200B 一次 flush），分片后每个
 *     在途事务都要一块内部 DMA 弹跳缓冲。
 *   失败特征：`draw_bitmap FAILED ... err=0x101 ext=1`，紧跟一条
 *   `internal DMA largest_free=... bytes`（BSP 诊断打印，直接给出余量）。
 *   另一半修复在 sdgoods_lvgl.c（通用 malloc 全部导向 PSRAM）—— 两处必须同改。
 *
 * 2026-09-20「EBADGE 打不开控制中心」定案（⚠️ 结论推翻了当天的两次误判，别再走回头路）：
 *   · 误判：以为「弹跳缓冲 = LCD_SPI_MAX_TRANSFER_SIZE」，于是 2048 -> 1024 -> 512 一降再降。
 *     实测三轮**全部照旧失败**，诊断恒为 largest_free=1664（只有 chunk 打印值跟着宏变）。
 *   · 真相：加探针打印 `spi_bus_get_max_transaction_len()` 得 **4092**（宏=512 也照样 4092），
 *     同一时刻试分配：128、256、512、1024 字节 -> OK；2048、4096、8192 字节 -> NULL。
 *     ⇒ 真实请求是 ~4096B，宏根本没起作用（见上方描述符取整说明）。
 *   · 正解：分片下限锁死在 4092，只能**腾内部 DMA**，不能靠缩小分片。队列 2 需 ≥8192B。
 *     EBADGE 里吃内存的是 WiFi 静态 RX 缓冲（10 x ~1.6KB ≈ 16KB，esp_wifi_init 时即分配），
 *     把 CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM 10->3（动态 RX/TX 32->16）后：
 *       largest_free: 1664B -> **13824B**（app_store 处实测），控制中心整屏重绘正常。
 *     WiFi/BLE 功能全部保留（扫描页、plane BLE 对战都在），只是 RX 缓冲档位更低。
 *   · 若将来 app 又变胖、余量重新掉到 8192B 以下，先查 WiFi/BLE 缓冲档位，
 *     再考虑把队列深度降到 1（需求减半到 4096B）—— 缩 LCD_SPI_MAX_TRANSFER_SIZE 是无效动作。 */
#define LCD_SPI_MAX_TRANSFER_SIZE  512

#define LEDC_HS_TIMER        LEDC_TIMER_0
#define LEDC_LS_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_CHANNEL  LEDC_CHANNEL_0
#define LEDC_ResolutionRatio LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty        ((1 << LEDC_ResolutionRatio) - 1)

extern esp_lcd_panel_handle_t panel_handle;

void sdgoods_lcd_init_panel(void);
void sdgoods_lcd_apply_vendor_madctl(void);
esp_err_t sdgoods_lcd_draw_bitmap_safe(int x1, int y1, int x2, int y2, const void *color_data);
/* 仅持指针，前向声明避免 sdgoods_lcd.h 强依赖 lvgl.h（实现见 sdgoods_lcd.c，已包含 lvgl.h） */
typedef struct _lv_disp_drv_t lv_disp_drv_t;
void sdgoods_lcd_set_lvgl_drv(lv_disp_drv_t *drv);
void sdgoods_lcd_set_backlight(uint8_t light);
uint8_t sdgoods_lcd_get_backlight(void);

/* 刷屏健康统计（`{dma!` 读走）：
 *  retry          = NO_MEM 重试后成功的分片次数（弹跳缓冲紧张，「差点花屏」）
 *  fail           = 重试 12 次仍失败、整块被丢的次数（真花屏）
 *  slow_wait      = done 信号量等待 >50ms 的次数（刷屏被 flash 写/高优任务拖住）
 *  worst_wait_ms  = 最长一次 done 等待
 *  min_dma_largest= 历次刷屏时见过的最小内部 DMA 连续块（判据：≥4096 才有分片可拿） */
void sdgoods_lcd_flush_stats(uint32_t *retry, uint32_t *fail, uint32_t *slow_wait,
                             uint32_t *worst_wait_ms, size_t *min_dma_largest);

/* 「排队前排空到的遗留完成信号」次数（2026-10-02 的竞态取证量）。
 * 背景：弹跳带路径原先在 draw_bitmap **之后**清完成信号量，而它与 LVGL 异步刷屏
 * 共用同一个信号量 ⇒ 一次带只有 ~0.2ms，若本次传输在「返回后、清信号前」完成，
 * 那个**属于本次**的信号就被自己吞掉 ⇒ 紧接着必然白等满超时（真机实测 506ms×21 次）。
 * 修法 = 改成计数信号量 + 排空挪到排队之前（见 sdgoods_lcd.c）。
 * 判据：修好后本值应稳定为 0；若持续增长说明还有别的路径在抢同一个信号量。 */
uint32_t sdgoods_lcd_stale_tokens(void);

/* 刷屏弹跳带**运行时开关**（调试键 `{band0!` / `{band1!`）。
 * 用途：在同一台设备 / 同一条链路上做「弹跳带开 vs 关」的下载吞吐 A/B ——
 * 弹跳带常驻 8KB 内部 DMA，Wi-Fi 动态缓冲也在同一块堆上，是否互相挤压
 * 只能当场来回切才测得准（换固件重烧则 Wi-Fi 环境已变，数据不可比）。
 * ⚠️ 关掉 = 回到「直接从 PSRAM 发」的老路 = **会花屏**，仅用于测速对照，
 *    测完务必 `{band1!` 切回来。关时缓冲会被 free，真正把 8KB 还回堆。 */
void sdgoods_lcd_band_enable(bool on);

/* 弹跳带**实际拿到多大**（0 = 没分配到，刷屏正在走会花屏的老路）。
 * `{dma!` 会把它打出来 —— 排查「屏又花了」时第一眼看这个：
 *   0    ⇒ 一次都没分配到（下载期连续块太小，连 1024 都拿不到）
 *   1024 ⇒ 已降级到底，屏不花但刷屏调用次数最多
 *   8192 ⇒ 满配 */
size_t sdgoods_lcd_band_bytes(void);

/* ---- TE（撕裂同步）门控 —— 2026-10-03 新增，治「切换壁纸有撕裂感」----------
 * 机理、为什么只门控整屏级、自愈策略：全在 sdgoods_lcd.c 的 te_align_if_big() 上方。
 * 这里只列对外契约：
 *   · 面板的 TE 输出由 lcd_qspi_bus_init() 末尾的 `0x35` 打开（GPIO18 = BOARD_LCD_SPI_IO_TE）；
 *   · **只有 ≥80% 屏像素的刷屏**才会等一次 TE 跳变（小区域不等，避免白丢帧率）；
 *   · 连续 4 次等不到就自愈关掉（代价上限 4×25ms），`dead` 会记下来。 */
void sdgoods_lcd_te_enable(bool on);

/* 主动采 `ms` 毫秒 TE 波形（`{te!`）：
 *   edges = 跳变次数；min_us / max_us = **相邻跳变间隔的最小/最大值**
 *   （模式 1 的 TE 一帧两个边沿 ⇒ 帧周期 = min + max，2026-10-03 实测为
 *     690µs + 15756µs = 16445µs ⇒ 面板 60.7Hz —— 只报"最后一次间隔"会得到
 *     690µs 这种误导值）；
 *   poll_max_us = 本次轮询间隔的最大值，**可不可信的判据**：正常 ≈10µs，
 *     若明显偏大说明本函数被高优先级任务抢占过 ⇒ min/max 是伪影、要重测
 *     （2026-10-03 实测到过 `min=282µs max=49031µs poll≈49ms` 那种"像 30Hz"的假数）；
 *   level = 采样结束时的引脚电平（-1 = GPIO 没配成功）。
 * 🔴 这是**唯一**能证明"TE 真的在工作"的手段：撕裂在静帧截图里看不见
 *    （截图读 LVGL 帧缓冲、不是面板回读），所以只能测波形 + 凭主观确认。 */
void sdgoods_lcd_te_probe(uint32_t ms, uint32_t *edges, uint32_t *min_us, uint32_t *max_us,
                          uint32_t *poll_max_us, int *level);

/* TE 门控的累计量（`{te!`）：
 *   waits   = 对齐尝试次数；timeouts = 其中超时次数（稳态应为 0）；
 *   starved = 超时里"被抢占"（而非"面板没信号"）的次数 —— 这类超时**不计**入自愈，
 *             否则一次 TLS 上行就能把门控误判成"没接"而永久关掉；
 *   skipped = 因"上一帧刚整屏刷过"（连续刷新 = 拖动/动画）而**跳过**对齐的次数 ——
 *             跳过是**设计行为**，不是故障：那种场景撕裂被运动掩盖，不值得加延迟。 */
void sdgoods_lcd_te_stats(bool *gate_on, bool *dead, uint32_t *waits, uint32_t *timeouts,
                          uint32_t *starved, uint32_t *skipped_motion, uint32_t *period_us,
                          int *level);
