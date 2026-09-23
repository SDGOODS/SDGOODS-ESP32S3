/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（BSP）· 多应用启动器 · 槽清单（manifest）
 *
 * 设备的 flash 才是槽真相源：每槽起始 +0x20 的 esp_app_desc_t。
 * 本文件把「flash 真相」缓存进 NVS 的 sdgoods_slots namespace，并在开机时自校验；
 * 同时实现「开机孤儿 appdata 清理」（设计稿 §2）。
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS). SPDX-License-Identifier: Apache-2.0
 */

#include "sdgoods_launcher_int.h"
#include "sdgoods_launcher.h"
#include "sdgoods_app_sdk.h"
#include "sdgoods_lcd.h"
#include "sdgoods_boot_skip.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"     /* esp_app_get_description()：权限闸门的日志里报本固件身份 */
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "sdgoods_audio.h"        /* sdgoods_audio_bgm_stop：返回启动器重启前先静音，避免硬切 BGM 爆音 */

static const char *TAG = "sdg_launcher";

/* ------------------------------------------------------------------ 槽 ↔ 分区 */
static int g_slot_count = 0;   /* 运行时确定，见 sdgoods_launcher_slot_count() */

int sdgoods_launcher_slot_count(void)
{
    if (g_slot_count == 0) {
        /* 实际 ota 分区数（不含 factory），受 CONFIG_SDGOODS_APP_SLOTS 与上限约束 */
        int n = esp_ota_get_app_partition_count();
#if CONFIG_SDGOODS_APP_SLOTS > 0
        if (n > CONFIG_SDGOODS_APP_SLOTS) {
            n = CONFIG_SDGOODS_APP_SLOTS;
        }
#endif
        if (n > SDGOODS_SLOT_COUNT_MAX) {
            n = SDGOODS_SLOT_COUNT_MAX;
        }
        if (n < 0) {
            n = 0;
        }
        g_slot_count = n;
    }
    return g_slot_count;
}

/* 声明在 sdgoods_launcher_int.h：同组件的 slot_cover.c 也要按同一口径找槽分区
 * （以前是 static，见该头的说明）。 */
const esp_partition_t *sdgoods_slot_partition(int idx)
{
    if (idx < 0 || idx >= sdgoods_launcher_slot_count()) {
        return NULL;
    }
    esp_partition_subtype_t sub = (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + idx);
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, NULL);
}

/* ------------------------------------------------------------- NVS 单槽读写 */
static const char *slot_key(int idx)
{
    /* 键名 "slot_0".."slot_15"：用静态缓冲，调用方需立即使用 */
    static char buf[12];
    snprintf(buf, sizeof(buf), "slot_%d", idx);
    return buf;
}

static esp_err_t slot_load(int idx, sdgoods_slot_entry_t *e)
{
    memset(e, 0, sizeof(*e));
    e->slot_idx = (uint8_t)idx;
    e->state = SDG_SLOT_EMPTY;
    nvs_handle_t h;
    esp_err_t r = nvs_open(SDGOODS_SLOTS_NVS_NS, NVS_READONLY, &h);
    if (r != ESP_OK) {
        return r;
    }
    size_t len = sizeof(*e);
    r = nvs_get_blob(h, slot_key(idx), e, &len);
    nvs_close(h);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;   /* 空槽，结构已清零 */
    }
    return r;
}

static esp_err_t slot_save(int idx, const sdgoods_slot_entry_t *e)
{
    nvs_handle_t h;
    esp_err_t r = nvs_open(SDGOODS_SLOTS_NVS_NS, NVS_READWRITE, &h);
    if (r != ESP_OK) {
        return r;
    }
    r = nvs_set_blob(h, slot_key(idx), e, sizeof(*e));
    if (r == ESP_OK) {
        r = nvs_commit(h);
    }
    nvs_close(h);
    return r;
}

/* ------------------------------------------------------------------ sha256 */
/* 一次性整包 sha256 的辅助已随 `install()` 改为流式而删除：现在用 mbedtls_sha256_* 的
 * 增量接口（见 s_install.sha），边收边算 —— 否则流式写入还得先把整包攒在内存里。 */

/* ----------------------------------------------------------- 单槽 flash 校验 */
/* 读槽上 esp_app_desc_t，返回 true 且填好 name/ver/sha；false 表示该槽无有效 app。 */
bool sdgoods_slot_read_desc(int idx, char *app_id, char *version, uint8_t *elf_sha)
{
    const esp_partition_t *part = sdgoods_slot_partition(idx);
    if (!part) {
        return false;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(part, &desc) != ESP_OK) {
        return false;
    }
    if (desc.magic_word != SDGOODS_APP_DESC_MAGIC) {
        return false;
    }
    if (app_id) {
        strncpy(app_id, desc.project_name, SDGOODS_SLOT_APPID_MAX - 1);
        app_id[SDGOODS_SLOT_APPID_MAX - 1] = '\0';
    }
    if (version) {
        strncpy(version, desc.version, SDGOODS_SLOT_VER_MAX - 1);
        version[SDGOODS_SLOT_VER_MAX - 1] = '\0';
    }
    if (elf_sha) {
        memcpy(elf_sha, desc.app_elf_sha256, 32);
    }
    return true;
}

/* ------------------------------------------------- 权限闸门（2026-09-19 新增）
 *
 * 「装 / 卸 / 启动某个槽」「回启动器 / 回 factory」都不是任何固件都该做的事：前者是
 * **启动器的簿记权**，后者只有**被启动器拉起的 app** 才需要（它重启后才能回到启动器）。
 * 以前这些 API 谁调都直接执行，后果不是「返回错误」而是**设备行为错乱**，且很难从现象
 * 倒推原因：
 *   · 单应用固件（含**自更新后运行在 `ota_N`** 的那种）调 `launch_slot(-1)`
 *     ⇒ `set_boot_partition(factory)` 指的其实就是它自己 + 重启 ⇒ 用户看到「莫名重启」，
 *       还顺带写下一个只有启动器才读的 skip 标志；
 *   · 任意 app 调 `install` / `uninstall` ⇒ 能改写别人的槽与 manifest ⇒ otadata 与
 *     manifest 错位（otadata 是启动器的簿记对象，见 sdgoods_launcher.h 的关键约定）。
 *
 * ⚠️ 判据必须走 `sdgoods_device_is_launcher_host()` / `_is_managed_app()`，**不能**简化成
 *    「运行分区不是 factory」—— 单应用固件自更新后也运行在 `ota_N` 上。判据由来与 2026-09-19
 *    真机实测见 sdgoods_device_mode.h。
 * ⚠️ **只读**查询（`slot_count` / `list_installed` / `find_free_slot`）**不加闸门**：它们不写
 *    flash、不切启动分区，控制中心数据页与 app 界面都要用。
 * ⚠️ 闸门只挡「越权调用」，不挡启动器自己：宿主的 UI 走的就是这些 API。
 */
static bool deny_unless_host(const char *what)
{
    if (sdgoods_device_is_launcher_host()) {
        return false;
    }
    const esp_app_desc_t *d = esp_app_get_description();
    ESP_LOGE(TAG, "%s refused: only the launcher host may do this (mode=%s, boot=%s, app='%s')",
             what, sdgoods_device_mode_str(), sdgoods_device_boot_partition(),
             (d && d->project_name[0]) ? d->project_name : "unknown");
    return true;
}

/* ----------------------------------------------------------- 开机自检 */
esp_err_t sdgoods_launcher_self_check(void)
{
    /* 自检会**写** manifest（NVS），且「以 flash 为准」的语义只在启动器设备上成立
     * ⇒ 闸门在宿主（见本文件顶部「权限闸门」）。 */
    if (deny_unless_host("self_check")) {
        return ESP_ERR_INVALID_STATE;
    }
    int n = sdgoods_launcher_slot_count();
    ESP_LOGI(TAG, "self-check: %d slots", n);
    for (int i = 0; i < n; i++) {
        sdgoods_slot_entry_t e;
        slot_load(i, &e);

        char name[SDGOODS_SLOT_APPID_MAX] = {0};
        char ver[SDGOODS_SLOT_VER_MAX] = {0};
        uint8_t elf_sha[32] = {0};
        bool ok = sdgoods_slot_read_desc(i, name, ver, elf_sha);

        if (ok) {
            strncpy(e.app_id, name, SDGOODS_SLOT_APPID_MAX - 1);
            e.app_id[SDGOODS_SLOT_APPID_MAX - 1] = '\0';
            strncpy(e.version, ver, SDGOODS_SLOT_VER_MAX - 1);
            e.version[SDGOODS_SLOT_VER_MAX - 1] = '\0';
            memcpy(e.sha256, elf_sha, 32);
            e.state = SDG_SLOT_INSTALLED;
            ESP_LOGI(TAG, "  slot %d: INSTALLED app='%s' v='%s'", i, name, ver);
        } else {
            if (e.app_id[0] != '\0') {
                /* manifest 说装了，但 flash 上读不到有效 app → 损坏 */
                ESP_LOGW(TAG, "  slot %d: flash missing, mark CORRUPTED (was '%s')", i, e.app_id);
                e.state = SDG_SLOT_CORRUPTED;
            } else {
                e.state = SDG_SLOT_EMPTY;
            }
            e.app_id[0] = '\0';
            e.version[0] = '\0';
            memset(e.sha256, 0, 32);
        }
        e.slot_idx = (uint8_t)i;
        slot_save(i, &e);
    }
    return ESP_OK;
}

/* ----------------------------------------------------------- 递归删除 */
/* 路径长度不会被 GCC 静态界定（path 来自调用方），这里用充足缓冲 + 局部关闭
 * -Wformat-truncation：appdata 目录深度至多两级，实测最长路径远小于缓冲，
 * 即使极端情况 snprintf 也只静默截断（不会溢出）。 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static esp_err_t recursive_remove(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        return (errno == ENOENT) ? ESP_OK : ESP_FAIL;
    }
    struct dirent *e;
    char child[384];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            recursive_remove(child);
        } else {
            remove(child);
        }
    }
    closedir(d);
    rmdir(path);
    return ESP_OK;
}
#pragma GCC diagnostic pop

/* --------------------------------------------------- 孤儿 appdata 清理 */
esp_err_t sdgoods_launcher_orphan_appdata_cleanup(void)
{
    /* ⚠️ **必须**闸门在宿主：本函数的判据是「appdata 里的目录名能不能对上某个已装槽」。
     * 单应用固件（或任何非启动器设备）上根本没有已装槽 ⇒ 全部目录都算「孤儿」⇒
     * **把 app 自己的数据整个删掉**，而且是在每次开机时删一次。这不是理论风险：
     * 平台层的 appdata 目录布局是共用的，app 用的就是 appdata/<app_id>/。
     * ⇒ 非宿主时直接拒绝，别让它有机会跑。 */
    if (deny_unless_host("orphan_appdata_cleanup")) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t m = sdgoods_appdata_mount();
    if (m != ESP_OK) {
        ESP_LOGW(TAG, "orphan-cleanup: appdata not mounted (%s), skip", esp_err_to_name(m));
        return m;
    }

    int n = sdgoods_launcher_slot_count();
    /* 收集当前所有「已装 app」的 app_id（flash 为准，来自刚刚的 self-check 缓存） */
    char keep[SDGOODS_SLOT_COUNT_MAX][SDGOODS_SLOT_APPID_MAX];
    int keep_n = 0;
    for (int i = 0; i < n; i++) {
        sdgoods_slot_entry_t e;
        slot_load(i, &e);
        if (e.state == SDG_SLOT_INSTALLED && e.app_id[0] != '\0' && keep_n < SDGOODS_SLOT_COUNT_MAX) {
            strncpy(keep[keep_n], e.app_id, SDGOODS_SLOT_APPID_MAX - 1);
            keep[keep_n][SDGOODS_SLOT_APPID_MAX - 1] = '\0';
            keep_n++;
        }
    }

    DIR *d = opendir(SDGOODS_APPDATA_BASE_PATH);
    if (!d) {
        sdgoods_appdata_unmount();
        return ESP_OK;   /* 还没有任何 app 数据 */
    }
    struct dirent *e;
    /* "/appdata" (8) + "/" + d_name(≤255) ≈ 264，留足余量 */
    char full[320];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        snprintf(full, sizeof(full), "%s/%s", SDGOODS_APPDATA_BASE_PATH, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;   /* 只清理「app_id 目录」 */
        }
        bool used = false;
        for (int k = 0; k < keep_n; k++) {
            if (!strcmp(keep[k], e->d_name)) {
                used = true;
                break;
            }
        }
        if (!used) {
            ESP_LOGI(TAG, "orphan appdata '%s' (no installed app) -> remove", e->d_name);
            recursive_remove(full);
        }
    }
    closedir(d);
    sdgoods_appdata_unmount();
    return ESP_OK;
}

esp_err_t sdgoods_launcher_boot_check(void)
{
    /* 开机自检只在**启动器设备**上有意义（别让 app 在 SINGLE 模式下调它：见
     * orphan_appdata_cleanup 的删数据警告）。两道闸门都在下游函数里，这里不重复判。 */
    esp_err_t r = sdgoods_launcher_self_check();
    if (r == ESP_OK) {
        sdgoods_launcher_orphan_appdata_cleanup();
    }
    return r;
}

/* ----------------------------------------------------------- 枚举 / 查空闲 */
/* ⚠️ list_installed / find_free_slot / slot_count 是**只读**查询：不写 flash、不切启动
 * 分区，故**不加权限闸门** —— 控制中心数据页（Slot n/N）与 app 自己的界面都要用它们。 */
esp_err_t sdgoods_launcher_list_installed(sdgoods_slot_entry_t *out, size_t cap, size_t *count)
{
    int n = sdgoods_launcher_slot_count();
    size_t c = 0;
    for (int i = 0; i < n && c < cap; i++) {
        sdgoods_slot_entry_t e;
        slot_load(i, &e);
        if (e.state == SDG_SLOT_INSTALLED) {
            out[c++] = e;
        }
    }
    if (count) {
        *count = c;
    }
    return ESP_OK;
}

int sdgoods_launcher_find_free_slot(void)
{
    int n = sdgoods_launcher_slot_count();
    for (int i = 0; i < n; i++) {
        sdgoods_slot_entry_t e;
        slot_load(i, &e);
        /* 空槽，或曾损坏（残留无效镜像）都可复用。
         * ⚠️ `UPDATING` **不算空闲**：那是「正在往这个槽写」的标记，把它交出去会让两个安装
         *    互相覆盖对方的数据。掉电残留的 UPDATING 不会永久占位 —— 开机自检
         *    `sdgoods_launcher_self_check()` 会按 flash 实况把它改成 EMPTY / CORRUPTED。 */
        if (e.state == SDG_SLOT_EMPTY || e.state == SDG_SLOT_CORRUPTED) {
            return i;
        }
    }
    return -1;
}

/* ----------------------------------------------------------- 安装 / 卸载 */

/* 流式安装的会话状态。放在文件作用域而不是堆：安装是一条线性流程，且这些字段
 * 必须在**每一条**失败路径上都能被清干净（否则下一个 begin 会永远返回 INVALID_STATE）。
 * 同一时刻只允许一个会话 —— 槽是独占资源，两个并发安装会把 manifest 写乱。 */
static struct {
    bool      active;
    int       slot_idx;
    size_t    total_len;
    size_t    written;
    char      app_id[SDGOODS_SLOT_APPID_MAX];
    char      version[SDGOODS_SLOT_VER_MAX];
    uint8_t   expect_sha[32];
    bool      has_sha;
    esp_ota_handle_t handle;
    mbedtls_sha256_context sha;
    sdgoods_slot_state_t prev_state;   /* 失败时回滚用 */
} s_install;

/* 安装失败 / 中途放弃时的收尾：擦净整槽 + manifest 置回 EMPTY。
 * 这里故意擦**整槽**而不是只擦已写入的部分：失败路径本就罕见，而「半个新镜像 + 尾部残留
 * 的旧镜像」会让「这个槽到底是什么」变得无法判断（manifest 说空，flash 上却能读出 desc）。 */
static void install_rollback(void)
{
    if (s_install.handle) {
        esp_ota_abort(s_install.handle);
        s_install.handle = 0;
    }
    const esp_partition_t *part = sdgoods_slot_partition(s_install.slot_idx);
    if (part) {
        esp_err_t r = esp_partition_erase_range(part, 0, part->size);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "install rollback: erase slot %d failed (%s)", s_install.slot_idx, esp_err_to_name(r));
        }
    }
    sdgoods_slot_entry_t e;
    memset(&e, 0, sizeof(e));
    e.slot_idx = (uint8_t)s_install.slot_idx;
    e.state = SDG_SLOT_EMPTY;
    slot_save(s_install.slot_idx, &e);

    if (s_install.active) {
        mbedtls_sha256_free(&s_install.sha);
    }
    s_install.active = false;
    s_install.written = 0;
    s_install.total_len = 0;
    s_install.handle = 0;
}

esp_err_t sdgoods_launcher_install_begin(int slot_idx, size_t total_len,
                                        const char *app_id, const char *version,
                                        const uint8_t sha256[32])
{
    /* 只有启动器有权往槽里写 app（它是槽与 manifest 的唯一簿记方）。
     * 闸门放在最前面：越权调用连 bin 都不用看。 */
    if (deny_unless_host("install_begin")) {
        ESP_LOGE(TAG, "  (install slot %d, app_id='%s')", slot_idx, app_id ? app_id : "-");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_install.active) {
        ESP_LOGE(TAG, "install_begin: a session is already active (slot %d, %u/%u bytes)",
                 s_install.slot_idx, (unsigned)s_install.written, (unsigned)s_install.total_len);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *part = sdgoods_slot_partition(slot_idx);
    if (!part) {
        return ESP_ERR_NOT_FOUND;
    }
    if (total_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 体积硬约束：先卡「安全线」，再卡分区大小。
     * 安全线是与平台 SAFE_APP_BYTES 的同口径（2.9MB）—— 理由见头文件。 */
    if (total_len > SDGOODS_SLOT_SAFE_BYTES || total_len > part->size) {
        ESP_LOGE(TAG, "install slot %d: %u bytes exceeds limit (safe=%u, slot=%u)",
                 slot_idx, (unsigned)total_len, (unsigned)SDGOODS_SLOT_SAFE_BYTES, (unsigned)part->size);
        return ESP_ERR_INVALID_SIZE;
    }

    sdgoods_slot_entry_t e;
    slot_load(slot_idx, &e);
    s_install.prev_state = e.state;

    /* 先把 manifest 标成 UPDATING 再动 flash：这样「掉电之后发现 manifest 是 UPDATING」
     * 才是可信的信号，开机自检据此把该槽判成损坏/空。顺序反过来就会有
     * 「镜像写了一半但 manifest 还写着上一个 app」的窗口。 */
    sdgoods_slot_entry_t upd = e;
    upd.slot_idx = (uint8_t)slot_idx;
    upd.state = SDG_SLOT_UPDATING;
    slot_save(slot_idx, &upd);

    s_install.slot_idx = slot_idx;
    s_install.total_len = total_len;
    s_install.written = 0;
    s_install.has_sha = (sha256 != NULL);
    if (sha256) {
        memcpy(s_install.expect_sha, sha256, 32);
    }
    memset(s_install.app_id, 0, sizeof(s_install.app_id));
    if (app_id && app_id[0]) {
        strncpy(s_install.app_id, app_id, sizeof(s_install.app_id) - 1);
    }
    memset(s_install.version, 0, sizeof(s_install.version));
    if (version && version[0]) {
        strncpy(s_install.version, version, sizeof(s_install.version) - 1);
    }

    /* OTA_WITH_SEQUENTIAL_WRITES ⇒ 不整槽擦，写到哪里擦到哪里（esp_ota_write 内部按扇区擦）。
     * 代价是调用方必须严格顺序写；收益是装小 app 不再付 3MB 的擦除时间。 */
    esp_err_t r = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &s_install.handle);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "install slot %d: esp_ota_begin failed (%s)", slot_idx, esp_err_to_name(r));
        s_install.active = false;
        sdgoods_slot_entry_t back = e;      /* 还没写 flash，恢复进入前的状态即可 */
        back.slot_idx = (uint8_t)slot_idx;
        back.state = s_install.prev_state;
        slot_save(slot_idx, &back);
        return r;
    }

    mbedtls_sha256_init(&s_install.sha);
    mbedtls_sha256_starts(&s_install.sha, 0);
    s_install.active = true;
    ESP_LOGI(TAG, "install slot %d: begin %u bytes (app_id='%s', v='%s', sha=%s)",
             slot_idx, (unsigned)total_len, s_install.app_id[0] ? s_install.app_id : "-",
             s_install.version[0] ? s_install.version : "-", sha256 ? "yes" : "no");
    return ESP_OK;
}

esp_err_t sdgoods_launcher_install_write(const void *data, size_t len)
{
    if (!s_install.active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 超出声明长度 = 调用方违约。**不写**，让 end() 走回滚路径（擦净整槽），
     * 否则会把下一个扇区（可能是别的槽或 otadata 的邻居）也写进去。 */
    if (s_install.written + len > s_install.total_len) {
        ESP_LOGE(TAG, "install slot %d: write overflow (%u + %u > %u)",
                 s_install.slot_idx, (unsigned)s_install.written, (unsigned)len,
                 (unsigned)s_install.total_len);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t r = esp_ota_write(s_install.handle, data, len);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "install slot %d: esp_ota_write failed at %u (%s)",
                 s_install.slot_idx, (unsigned)s_install.written, esp_err_to_name(r));
        return r;
    }
    mbedtls_sha256_update(&s_install.sha, data, len);
    s_install.written += len;
    return ESP_OK;
}

esp_err_t sdgoods_launcher_install_end(void)
{
    if (!s_install.active) {
        return ESP_ERR_INVALID_STATE;
    }
    const int slot_idx = s_install.slot_idx;

    /* 1) 长度必须与声明一致（少了就是传输中断，多了在 write 里已被拒） */
    if (s_install.written != s_install.total_len) {
        ESP_LOGE(TAG, "install slot %d: incomplete (%u/%u bytes)",
                 slot_idx, (unsigned)s_install.written, (unsigned)s_install.total_len);
        install_rollback();
        return ESP_ERR_INVALID_SIZE;
    }

    /* 2) sha256：边写边算到这里才比对 —— 流式也做完整性校验 */
    uint8_t calc[32];
    mbedtls_sha256_finish(&s_install.sha, calc);
    if (s_install.has_sha && memcmp(calc, s_install.expect_sha, 32) != 0) {
        ESP_LOGE(TAG, "install slot %d: sha256 mismatch", slot_idx);
        install_rollback();
        return ESP_ERR_INVALID_CRC;
    }

    /* 3) esp_ota_end：按 esp_app_desc_t 校验镜像本身（magic / elf sha / 长度） */
    esp_err_t r = esp_ota_end(s_install.handle);
    s_install.handle = 0;
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "install slot %d: esp_ota_end failed (%s)", slot_idx, esp_err_to_name(r));
        install_rollback();
        return r;
    }

    /* 4) 以 flash 为准回填 manifest（app_id 优先用平台下发的 Firmware.id） */
    char name[SDGOODS_SLOT_APPID_MAX] = {0};
    char ver[SDGOODS_SLOT_VER_MAX] = {0};
    if (!sdgoods_slot_read_desc(slot_idx, name, ver, NULL)) {
        ESP_LOGE(TAG, "install slot %d: desc unreadable after write", slot_idx);
        install_rollback();
        return ESP_ERR_INVALID_STATE;
    }

    sdgoods_slot_entry_t e;
    memset(&e, 0, sizeof(e));
    e.slot_idx = (uint8_t)slot_idx;
    if (s_install.app_id[0]) {
        strncpy(e.app_id, s_install.app_id, sizeof(e.app_id) - 1);
    } else {
        strncpy(e.app_id, name, sizeof(e.app_id) - 1);
    }
    strncpy(e.version, s_install.version[0] ? s_install.version : ver, sizeof(e.version) - 1);
    memcpy(e.sha256, s_install.has_sha ? s_install.expect_sha : calc, 32);
    e.state = SDG_SLOT_INSTALLED;
    e.last_launch = 0;
    esp_err_t sr = slot_save(slot_idx, &e);

    mbedtls_sha256_free(&s_install.sha);
    s_install.active = false;
    s_install.written = 0;
    s_install.total_len = 0;
    ESP_LOGI(TAG, "install slot %d: INSTALLED app='%s' v='%s'", slot_idx, e.app_id, e.version);
    return sr;
}

esp_err_t sdgoods_launcher_install(int slot_idx, const uint8_t *bin, size_t len,
                                   const char *app_id, const char *version,
                                   const uint8_t sha256[32])
{
    if (!bin || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t r = sdgoods_launcher_install_begin(slot_idx, len, app_id, version, sha256);
    if (r != ESP_OK) {
        return r;
    }
    r = sdgoods_launcher_install_write(bin, len);
    if (r != ESP_OK) {
        sdgoods_launcher_install_end();   /* 收尾/回滚；返回首个错误更有诊断价值 */
        return r;
    }
    return sdgoods_launcher_install_end();
}

esp_err_t sdgoods_launcher_uninstall(int slot_idx)
{
    /* 卸载会擦槽 + 删 appdata ⇒ 同样只有启动器有权（闸门见本文件顶部）。 */
    if (deny_unless_host("uninstall")) {
        ESP_LOGE(TAG, "  (uninstall slot %d)", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *part = sdgoods_slot_partition(slot_idx);
    if (!part) {
        return ESP_ERR_NOT_FOUND;
    }
    sdgoods_slot_entry_t e;
    slot_load(slot_idx, &e);
    char app_id[SDGOODS_SLOT_APPID_MAX];
    strncpy(app_id, e.app_id, sizeof(app_id) - 1);
    app_id[sizeof(app_id) - 1] = '\0';

    /* 1) 先把 otadata 从这个槽上挪开（如果它正指着这个槽），再擦。
     *
     * 为什么必须有这一步：`otadata` 是「下次启哪个分区」的唯一依据，而**只有启动器会写它**。
     * 用户在启动器里卸载掉「上次进过的那个 app」时，otadata 仍指着它 —— 擦完重启，
     * bootloader 只会去找一个空分区，日志是 `no bootable app partitions in the partition table`，
     * 靠「无效镜像回退到 factory」兜底才回到启动器。表现是黑屏几秒或一次重启，
     * 而且 manifest/otadata 就此长期错位（平台 sync 到的槽清单与设备实际启动行为对不上）。
     * 先挪 otadata 再擦：中途掉电最坏也只是「otadata 指 factory + 槽半擦」，下次开机自检修好。 */
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot && boot->address == part->address) {
        const esp_partition_t *factory =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
        if (factory) {
            esp_err_t rb = esp_ota_set_boot_partition(factory);
            ESP_LOGW(TAG, "uninstall slot %d: otadata pointed at this slot -> boot back to factory (%s)",
                     slot_idx, esp_err_to_name(rb));
        }
    }

    /* 2) 擦槽 */
    esp_err_t r = esp_partition_erase_range(part, 0, part->size);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "uninstall slot %d: erase failed (%s)", slot_idx, esp_err_to_name(r));
    }

    /* 3) 清 manifest */
    memset(&e, 0, sizeof(e));
    e.slot_idx = (uint8_t)slot_idx;
    e.state = SDG_SLOT_EMPTY;
    slot_save(slot_idx, &e);

    /* 4) 清 appdata/<app_id>/（决策 #3：卸载直接清，隐私默认不保留）
     *
     * ⚠️ **必须先挂载**：appdata 是 FAT 分区，只有挂载后才有 VFS 路径。不挂载时
     *    `opendir()` 直接失败、`recursive_remove()` 干净地返回「没什么可删」
     *    ⇒ 表现为**卸载后数据还在，而且不报任何错**（2026-09-19 真机抓到：
     *    卸载 SDGOODS_EBADGE 之后它的 appdata 目录仍在，直到下次开机才被
     *    `sdgoods_launcher_orphan_appdata_cleanup()` 扫掉 —— 也就是说决策 #3
     *    此前一直靠兜底生效，卸载本身漏了这一步，而且漏得完全没有声音）。
     * 挂载是幂等的（s_wl != INVALID 直接返回 OK），卸载动作只发生在「该 app 已不在
     * 运行」的时刻（切 app = 重启），所以这里 mount/unmount 一对是安全的。 */
    if (app_id[0] != '\0') {
        esp_err_t m = sdgoods_appdata_mount();
        if (m != ESP_OK) {
            ESP_LOGW(TAG, "uninstall slot %d: appdata not mounted (%s) -> '%s' left for boot orphan sweep",
                     slot_idx, esp_err_to_name(m), app_id);
        } else {
            char full[96];
            snprintf(full, sizeof(full), "%s/%s", SDGOODS_APPDATA_BASE_PATH, app_id);
            struct stat st;
            const bool existed = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
            esp_err_t rr = recursive_remove(full);
            /* 这条日志是「决策 #3 真的生效了」的唯一凭证：existed 为 false 时无需紧张
             * （该 app 从没写过数据），但出现 existed=true 才说明这次真清掉了东西。 */
            ESP_LOGI(TAG, "uninstall slot %d: appdata '%s' %s (%s)", slot_idx, app_id,
                     existed ? "removed" : "absent", esp_err_to_name(rr));
            sdgoods_appdata_unmount();
        }
    }
    return ESP_OK;
}

/* ----------------------------------------------------------- 切换 / 返回 */
esp_err_t sdgoods_launcher_launch_slot(int idx)
{
    /* ---- 权限闸门（两个方向分开判，理由见本文件顶部「权限闸门」） ---- */
    if (idx >= 0) {
        /* 启动某个槽 = 改写 otadata ⇒ 只有启动器宿主有权做。 */
        if (deny_unless_host("launch_slot")) {
            ESP_LOGE(TAG, "  (launch_slot %d)", idx);
            return ESP_ERR_INVALID_STATE;
        }
    } else if (!sdgoods_device_is_managed_app()) {
        /* 回 factory（= 回启动器）只有「被启动器管理的 app」执行才有意义 —— 它重启后才
         * 回得去启动器。宿主自己调 = 重启进自己；**单应用固件**（含自更新后运行在 ota_N
         * 的那种）调 = set_boot_partition(factory) 指向自己 + 重启 ⇒ 用户看到「莫名重启」，
         * 还以为「刚更新的固件被退回去了」（2026-09-19 真机事故的另一半；判据由来见
         * sdgoods_device_mode.h）。⇒ 直接拒绝，不写 otadata、不重启。 */
        const esp_app_desc_t *d = esp_app_get_description();
        ESP_LOGE(TAG, "return-to-launcher refused: this firmware is not a launcher-managed app "
                      "(mode=%s, boot=%s, app='%s') -- restarting would only come back to itself. "
                      "Expected for a single-app firmware, including one that just did A/B OTA.",
                 sdgoods_device_mode_str(), sdgoods_device_boot_partition(),
                 (d && d->project_name[0]) ? d->project_name : "unknown");
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *part;
    if (idx < 0) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    } else {
        part = sdgoods_slot_partition(idx);
    }
    if (!part) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t r = esp_ota_set_boot_partition(part);
    if (r != ESP_OK) {
        return r;
    }
    if (idx >= 0) {
        /* 记录最近启动时间（NVS 缓存，平台 sync 时取用） */
        sdgoods_slot_entry_t e;
        slot_load(idx, &e);
        e.last_launch = (uint32_t)(esp_log_timestamp() / 1000);   /* 近似秒级，足够排序 */
        slot_save(idx, &e);
    }
    /* 关背光：消除「重启间隙旧帧一直亮到 bootloader 清屏」造成的闪屏。 */
    sdgoods_lcd_set_backlight(0);
    /* 返回启动器（idx<0）时，下次启动器开机跳过开机动画直进主页；进 app 则清标志。 */
    sdgoods_boot_set_skip_next(idx < 0);
    esp_restart();   /* 不返回 */
    return ESP_OK;
}

esp_err_t sdgoods_launcher_return_to_launcher(void)
{
    /* 权限闸门在 launch_slot(-1) 里（唯一落点，不重复判）：只有「被启动器管理的 app」
     * 调用才会真的重启，其它固件（单应用固件 / 启动器宿主）调它返回 ESP_ERR_INVALID_STATE
     * 并打一条 `return-to-launcher refused: ...` 日志，**不重启**。 */
    /* 先静音：从「正在播放 BGM 的 app」返回启动器会触发 esp_restart，
     * 硬切断音频 DMA 会让功放从有信号瞬间跌落为静音 → 产生「啪」的爆音。
     * 与关机路径 pwr_off_async 同理，重启前先优雅渐出 BGM 并拉低功放/喇叭使能。
     * sdgoods_audio_bgm_stop 对已停止状态幂等（仅确保 PA/spk 关闭），此处调用安全，
     * 也覆盖了「电源键短按退出 app」这条原本未静音的路径。 */
    sdgoods_audio_bgm_stop();
    return sdgoods_launcher_launch_slot(-1);
}
