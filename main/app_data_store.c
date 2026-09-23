/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

#include "app_data_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"

#include "sdgoods_app_sdk.h"   /* sdgoods_appdata_begin / _available */
#include "sdgoods_nvs.h"       /* sdgoods_nvs_ensure：平台层统一的 NVS 保障 */
#include "sdgoods_lcd.h"       /* LCD_SPI_MAX_TRANSFER_SIZE / LCD_SPI_TRANS_QUEUE_SZ：弹跳预算 */

#include "esp_app_desc.h"      /* ⚠️ 是 esp_app_desc.h，不是 esp_app_format.h ——
                                     esp_app_desc_t 与 esp_app_get_description() 都在这里，
                                     写另一个名字会得到 "unknown type name 'esp_app_desc_t'" */

static const char *TAG = "app_store";

/* ⚠️ 身份（app_id）必须**动态取 esp_app_desc.project_name**，不要写死。
   曾经这里是一行 `#define APP_ID "SDGOODS_EBADGE"` —— 于是 app 的身份变成**两份**：
   数据目录用的是这个写死的常量，而卸载/孤儿清理用的是 project_name。
   开发者改了 CMakeLists.txt 的 project() 却漏改这一行（参考实现就是最容易被照抄的地方）
   ⇒ 数据写进 A 目录、卸载删的是 B 目录：数据互串，且卸载后残留垃圾。
   动态取就不会有这样的「两份」，改 project() 一处即全站生效。 */
static const char *my_app_id(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->project_name : "unknown_app";
}

#define APP_ID          my_app_id()

#define NVS_NS          "appstor"
#define NVS_KEY_BOOTS   "boots"
#define FAT_FILE        "boots.txt"

static char        s_dir[64];        /* appdata 可用时的私有目录，否则空串 */
static const char *s_backend = "--";
static uint32_t    s_boots;

/* ---- 后端 A：appdata(FAT) 文件 -------------------------------------------------
 * 适合大对象（日志/音频/缓存）与多文件。注意这里是**文件**不是键值，
 * 想存结构化配置就自己定格式（本示例用一行十进制数字，够用且好对日志）。 */
static bool try_appdata(void)
{
    char dir[sizeof(s_dir)];
    if (sdgoods_appdata_begin(APP_ID, dir, sizeof(dir)) != ESP_OK) {
        return false;   /* 没有分区 / 挂载失败 —— 这是**正常返回值**，走 NVS 即可 */
    }
    snprintf(s_dir, sizeof(s_dir), "%s", dir);

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", s_dir, FAT_FILE);

    unsigned n = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%u", &n) != 1) {
            n = 0;
        }
        fclose(f);
    }
    n++;
    f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "write '%s' failed -> falling back to NVS", path);
        s_dir[0] = '\0';
        return false;
    }
    fprintf(f, "%u\n", n);
    fclose(f);

    s_boots = (uint32_t)n;
    s_backend = "appdata";
    ESP_LOGI(TAG, "backend=appdata dir='%s' boots=%u", s_dir, (unsigned)s_boots);
    return true;
}

/* ---- 后端 B：NVS 键值 ---------------------------------------------------------
 * 适合几百字节以内的小配置。⚠️ 别用自己手写的 nvs_flash_init —— 最小 app 不碰
 * WiFi/BLE 时从不初始化 NVS，平台统一入口 sdgoods_nvs_ensure() 会负责擦除重试。 */
static void use_nvs(void)
{
    sdgoods_nvs_ensure();          /* 幂等，重复调用安全 */
    nvs_handle_t h;
    esp_err_t r = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s, boot count not persisted",
                 NVS_NS, esp_err_to_name(r));
        s_backend = "NVS(ro)";
        return;
    }
    uint32_t n = 0;
    nvs_get_u32(h, NVS_KEY_BOOTS, &n);   /* 首次上电没这个键，属正常 */
    n++;
    nvs_set_u32(h, NVS_KEY_BOOTS, n);
    nvs_commit(h);
    nvs_close(h);

    s_boots = n;
    s_backend = "NVS";
    ESP_LOGI(TAG, "backend=NVS ns='%s' key='%s' boots=%u", NVS_NS, NVS_KEY_BOOTS,
             (unsigned)s_boots);
}

void app_data_store_init(void)
{
    /* 先打一句「本固件的分区表里有没有 appdata」—— 排查「数据没留住」时，
     * 这一行能立刻区分「兜底没生效」还是「兜底生效了但你没读对」。
     * ⚠️ 它只查分区表、不挂载，放在任何阶段调用都安全。 */
    const bool has = sdgoods_appdata_available();
    ESP_LOGI(TAG, "appdata partition present=%s", has ? "yes" : "no");

    /* 挂载 FAT + 磨损均衡会**常驻**占一块内部 RAM，而「内部 DMA 最大连续块」是显示管线
     * 的硬预算（每个 SPI 分片要一块弹跳缓冲，预算 = LCD_SPI_MAX_TRANSFER_SIZE ×
     * LCD_SPI_TRANS_QUEUE_SZ = 4096B，见 sdgoods_lcd.h）。所以前后各打一次余量，
     * 把这次挂载的**真实代价**量出来 —— 2026-09-19 实测 app0 上掉约 30KB。
     * 谁要再往这个 app 里加 FAT/网络/大缓冲，先看这两行数字再决定。 */
    const size_t dma_before = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const bool used_fat = has && try_appdata();
    const size_t dma_after = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "internal DMA largest_free: before=%u after=%u bytes (cost=%d, SPI bounce budget needs %d)",
             (unsigned)dma_before, (unsigned)dma_after, (int)dma_before - (int)dma_after,
             (int)(LCD_SPI_MAX_TRANSFER_SIZE * LCD_SPI_TRANS_QUEUE_SZ));

    if (used_fat) {
        return;
    }
    use_nvs();
}

const char *app_data_store_backend(void)
{
    return s_backend;
}

const char *app_data_store_dir(void)
{
    return s_dir;
}

unsigned app_data_store_boot_count(void)
{
    return s_boots;
}

float app_data_store_free_mb(void)
{
    if (!s_dir[0]) {
        return 0.f;   /* 没有 appdata：调用方应显示后端名而不是数字 */
    }
    /* 去掉最后一段（/<app_id>）拿到挂载点 —— 不写死 "/appdata" 字面量。 */
    char base[sizeof(s_dir)];
    snprintf(base, sizeof(base), "%s", s_dir);
    char *slash = strrchr(base, '/');
    if (!slash || slash == base) {
        return 0.f;
    }
    *slash = '\0';

    /* 控制中心的数据页会「挂载 → 读 → 卸载」，所以这里必须再确保一次挂载
     * （sdgoods_appdata_mount 是幂等的，已挂载时直接返回 OK）。 */
    if (sdgoods_appdata_mount() != ESP_OK) {
        return 0.f;
    }
    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(base, &total, &freeb) != ESP_OK) {
        return 0.f;
    }
    return (float)((double)freeb / (1024.0 * 1024.0));
}
