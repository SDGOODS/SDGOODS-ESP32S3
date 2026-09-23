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
 * 取 2 是为了流水掩盖「弹跳分配 + memcpy」间隙。启动器同点位约 31KB，余量充足；
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
