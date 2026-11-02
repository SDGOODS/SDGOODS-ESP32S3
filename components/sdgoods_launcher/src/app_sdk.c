/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（BSP）· 应用 SDK（提供给上架 app 的极薄库）
 *
 * 实现 sdgoods_app_sdk.h：返回启动器 + appdata（高 16M 按 app_id 隔离）。
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS). SPDX-License-Identifier: Apache-2.0
 */

#include "sdgoods_app_sdk.h"
#include "sdgoods_launcher.h"
#include "sdgoods_launcher_int.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "sdg_app_sdk";
static wl_handle_t s_wl = WL_INVALID_HANDLE;

/* ===================== appdata 挂载/卸载的并发纪律（2026-10-12 定案） =====================
 *
 * 【现象】软复位（`esp_restart()` / 控制台 'R'）之后**几乎必崩**，冷启动却看不出来：
 *     Guru Meditation Error: Core 0 panic'ed (Cache error)
 *     Dbus write to cache rejected, error address: 0x3c1483ac   ← 只读字面量 "appdata"
 *   8/8 轮 100% 复现，回溯每次完全一致（ELF SHA 已对齐）：
 *     wp_task → wp_load_slot → wp_read_file → fs_ready
 *     → sdgoods_appdata_mount (本文件 :74)
 *     → esp_vfs_fat_spiflash_mount_rw_wl → s_f_mount_rw → f_mount (ff.c:3699 `fs->pdrv = ...`)
 *
 * 【机理】两条 IDF 代码凑成一把枪 —— 我们只是扣了扳机：
 *   ① `esp_vfs_fat_register_cfg()`（vfs_fat.c:187-190）在 base_path **已注册**时
 *      直接 `return ESP_ERR_INVALID_STATE`，**根本没有给 `*out_fs` 赋值**；
 *   ② `esp_vfs_fat_spiflash_mount_internal()`（vfs_fat_spiflash.c:177-192）里
 *      `FATFS *fs;` 是**未初始化**的局部量，它把这个 INVALID_STATE 当成
 *      "没事，已经注册过了"吞掉，然后照样把 `fs` 递给 `f_mount()`。`f_mount`
 *      在"注册新 fs 对象"分支执行 `fs->pdrv = LD2PD(vol)` ⇒ 写进那个垃圾指针。
 *      实测垃圾值 = 栈上残留的 `partition_label`（= `SDGOODS_APPDATA_PART_LABEL`
 *      = "appdata"），所以崩点地址正好是那个只读字面量的首字节。
 *
 * 【触发条件】只要出现「**`/appdata` 已注册，但又有人去 mount 一次**」就中枪。
 *   我们原来的 `mount()` 是「先查 `s_wl` 再挂」的 check-then-act，两个任务同时进就重了。
 *   开机时那两位是：主任务的 `boot_check → 孤儿 appdata 清理` 与
 *   壁纸任务的 `wp_read_file → fs_ready`。
 *   🔴 真机判据（崩的那次 vs 不崩的两次，同一次抓包）：
 *     崩：孤儿清理在 t=2505 ms，壁纸还在 t=1705/2215/2665 连续加载槽位 ⇒ **两窗口重叠**；
 *     不崩：清理在 t=6576 / 6815 ms，壁纸 4186 / 4215 ms 就收工了 ⇒ 差 2.4 s，不重叠。
 *   ⇒ 而"重叠与否"只取决于**开机是否走快路径**：软复位没有 4.3 s 开机动画，
 *     `boot_check` 提前到 2.5 s 就撞上壁纸的读盘序列。这就是"平时冷启动看着没问题"的原因。
 *
 * 【不变式】修法只有一句：让下面这条永远成立 ——
 *      `s_wl != WL_INVALID_HANDLE`  ⟺  `/appdata` 已注册到 VFS
 *   ① **加互斥**：check-then-act 变原子，两个任务不可能同时进 `mount()`；
 *   ② **unmount 不再真卸**（见其函数体的长注释）。
 *   有了 ①，`s_wl == INVALID` 时 `/appdata` 必然未注册 ⇒ IDF 那边的注册一定成功
 *   ⇒ ① 那条 INVALID_STATE 永远走不到 ⇒ `fs` 永远是有效指针。
 *
 * ⚠️ 别再引入"卸载 → 再挂载"的来回：`s_wl` 是**单句柄 + 幂等**，`unmount()` 根本
 *    无法判断自己是不是最后一个用户（壁纸/devlink/pushstore 都是 mount 完就一直用），
 *    所以「借完就还」这个模式在本卷上**天生不安全**。要真卸必须先做引用计数，
 *    而那需要把所有消费者改成 acquire/release —— 不是本文件能单独完成的事。
 *
 * ⚠️ 锁必须**静态**（`xSemaphoreCreateMutexStatic`，配置见
 *    `CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=1`）：与 `sdgoods_cmd.c` 的
 *    `s_cmd_stack` 同一条理由 —— 本工程内部堆被切得很碎，静态量在 .bss 里不参与堆分配。
 */
static SemaphoreHandle_t s_appdata_mtx;
static StaticSemaphore_t s_appdata_mtx_buf;
static portMUX_TYPE      s_appdata_mtx_guard = portMUX_INITIALIZER_UNLOCKED;

/** 惰性建锁。临界区里只做「判空 + 指针赋值 + 静态创建」，全都不阻塞。 */
static SemaphoreHandle_t appdata_mtx(void)
{
    if (s_appdata_mtx) {
        return s_appdata_mtx;
    }
    portENTER_CRITICAL(&s_appdata_mtx_guard);
    if (!s_appdata_mtx) {
        s_appdata_mtx = xSemaphoreCreateMutexStatic(&s_appdata_mtx_buf);
    }
    portEXIT_CRITICAL(&s_appdata_mtx_guard);
    return s_appdata_mtx;
}

/* ★ 编译期红线：appdata 的目录名必然超 FAT 的 8.3 短名限制。
 *
 * `sdgoods_appdata_begin()` 要建 /appdata/<app_id>/，而 app_id = esp_app_desc_t.project_name
 * （最长 32 字符，本工程即 "SDGOODS_EBADGE" 14 字符）。IDF 默认不开长文件名
 * （CONFIG_FATFS_LFN_NONE=y）时 f_mkdir 返回 FR_INVALID_NAME ⇒ VFS 报 EINVAL(22)
 * ⇒ 目录建不出来 ⇒ **app 的持久化数据静默落空**，而且只有 ≤8 字符的 app_id 看着正常，
 * 用短名字的示例去验证会「通过」，极难发现（2026-09-19 真机抓到）。
 * 修法：工程 sdkconfig **与** sdkconfig.defaults 都设 CONFIG_FATFS_LFN_HEAP=y。 */
#if defined(CONFIG_FATFS_LFN_NONE)
#warning "CONFIG_FATFS_LFN_NONE=y: appdata/<app_id> 目录建不出来（app_id 通常 > 8 字符）。请在 sdkconfig 与 sdkconfig.defaults 里设 CONFIG_FATFS_LFN_HEAP=y（见 docs/BUILD.md §2.1）。"
#endif

esp_err_t sdgoods_return_to_launcher(void)
{
    /* 直接复用启动器的「回 factory 并重启」逻辑（设计稿 §3）。
     * ⚠️ 模式闸门在 sdgoods_launcher_launch_slot(-1) 里：非「被启动器管理的 app」
     *    （单应用固件 / 启动器宿主）会拿到 ESP_ERR_INVALID_STATE，**不重启**。 */
    return sdgoods_launcher_return_to_launcher();
}

bool sdgoods_appdata_available(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_FAT,
                                    SDGOODS_APPDATA_PART_LABEL) != NULL;
}

/** 真正的挂载体。**调用者必须已持有 `appdata_mtx()`**（见上方并发纪律）。 */
static esp_err_t appdata_mount_locked(void)
{
    if (s_wl != WL_INVALID_HANDLE) {
        return ESP_OK;   /* 已挂载，幂等 */
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                                 SDGOODS_APPDATA_PART_LABEL);
    if (!part) {
        ESP_LOGE(TAG, "appdata partition '%s' (data/fat) not found",
                 SDGOODS_APPDATA_PART_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,   /* 首次上电为空分区，格式化即可 */
        .max_files = 8,
        .allocation_unit_size = 4096,
    };
    wl_handle_t wl = WL_INVALID_HANDLE;
    esp_err_t r = esp_vfs_fat_spiflash_mount_rw_wl(SDGOODS_APPDATA_BASE_PATH,
                                                   SDGOODS_APPDATA_PART_LABEL, &cfg, &wl);
    if (r != ESP_OK) {
        /* IDF 的 `fail:` 标号自己会 `esp_vfs_fat_unregister_path()` ⇒ 状态仍一致，
         * 下次重试是安全的（互斥保证了不会有第二个 mount 同时进场）。 */
        return r;
    }
    s_wl = wl;
    return ESP_OK;
}

esp_err_t sdgoods_appdata_mount(void)
{
    SemaphoreHandle_t m = appdata_mtx();
    if (!m) {
        return appdata_mount_locked();   /* 建锁失败：退化成原行为（理论上到不了） */
    }
    xSemaphoreTake(m, portMAX_DELAY);
    const esp_err_t r = appdata_mount_locked();
    xSemaphoreGive(m);
    return r;
}

void sdgoods_appdata_unmount(void)
{
    /* 🔴 故意**不真卸** —— 理由见本文件顶部「appdata 挂载/卸载的并发纪律」。
     *
     * 两条，都是这次真机量出来的：
     *  ① 语义上「借完就还」在本卷**不成立**：`s_wl` 是单句柄、`mount()` 又幂等，
     *     卸载方**无法知道还有没有别人在用**（壁纸 / devlink / pushstore 都是
     *     mount 完就一直读写）。真卸就等于把卷从别人脚下抽走。
     *  ② 真卸**不可靠**：`esp_vfs_fat_spiflash_unmount_rw_wl()` 在"分区没注册"时
     *     会提前 `return ESP_ERR_INVALID_STATE` **什么都不做**（vfs_fat_spiflash.c:229-236），
     *     也可能在"VFS 已注销、wl_unmount 失败"时返回非 OK。两种情况下调用方若照旧
     *     清 `s_wl`，就造出「`s_wl` 无效但 `/appdata` 仍注册」⇒ 下一次 `mount()`
     *     必中 IDF 那条未初始化的 `fs`（= 本次崩溃）。**这是一条不依赖竞态的必崩路径。**
     *
     * 代价：FATFS/VFS 常驻（一个 ctx + 8 个 FIL 槽，几百字节堆）。而启动器本来
     * **每次开机都要用** `/appdata`（壁纸、Wi-Fi 凭据、push 列表、语言包），
     * 所以"省下这次挂载"没有任何实际收益。
     *
     * ⚠️ 将来真要支持"用完就卸"，必须先把**所有**消费者改成 acquire/release 引用计数，
     *    并且只在计数归零且卸载**确认成功**时才清 `s_wl` —— 不能只改本函数。
     */
    static bool noted;
    if (!noted) {
        noted = true;
        ESP_LOGI(TAG, "unmount('/appdata') 按设计忽略（改为常驻挂载）——理由见 app_sdk.c 顶部注释");
    }
}

esp_err_t sdgoods_appdata_path_for(const char *app_id, char *out, size_t out_len)
{
    if (!app_id || !app_id[0] || !out || out_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(out, out_len, "%s/%s", SDGOODS_APPDATA_BASE_PATH, app_id);
    if (n < 0 || (size_t)n >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t sdgoods_appdata_begin(const char *app_id, char *out, size_t out_len)
{
    esp_err_t r = sdgoods_appdata_mount();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "appdata unavailable (%s) -> caller must fall back to NVS",
                 esp_err_to_name(r));
        return r;
    }
    r = sdgoods_appdata_path_for(app_id, out, out_len);
    if (r != ESP_OK) {
        return r;
    }
    if (mkdir(out, 0777) != 0 && errno != EEXIST) {
        int e = errno;
        ESP_LOGE(TAG, "mkdir('%s') failed: errno=%d", out, e);
        /* EINVAL(22) 几乎总是同一个原因：FAT 没开长文件名，而 app_id > 8 字符。
         * 直接把修法写进日志，省掉一轮「errno=22 是什么意思」的排查。 */
        if (e == EINVAL) {
            ESP_LOGE(TAG, "  ↳ EINVAL 多半是 FAT 未开长文件名：app_id '%s' 超过 8.3 短名限制。",
                     app_id);
            ESP_LOGE(TAG, "    请设 CONFIG_FATFS_LFN_HEAP=y（sdkconfig 与 sdkconfig.defaults 都要改）"
                          "，见 docs/BUILD.md §2.1");
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "appdata ready for '%s': %s", app_id, out);
    return ESP_OK;
}
