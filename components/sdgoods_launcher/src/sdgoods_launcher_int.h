/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 多应用启动器 · 内部共享定义（不对外）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_partition.h"

/* ---- 槽 ↔ 分区：本组件内部共享的两个只读原语 ----
 *
 * 槽号 ↔ 分区的换算、以及「读槽上 esp_app_desc_t」都只有一处实现（slot_manifest.c），
 * 由同组件的其它源文件按名调用。以前它们是 static，加 slot_cover.c 时若各自再写一份
 * 「ESP_PARTITION_SUBTYPE_APP_OTA_0 + idx」就会埋下漂移：两边对槽的认知一旦不同，
 * 表现是「封面显示在别的 app 头上」这种极难倒推的问题。
 * ⚠️ 都**不加权限闸门**（只读查询），任何固件可调。 */

/* 槽 idx 对应的 ota_N 分区；越界或分区不存在返回 NULL。 */
const esp_partition_t *sdgoods_slot_partition(int idx);

/* 读槽上 esp_app_desc_t。返回 true 表示该槽有合法 app 且已填好传入的非空出参。
 * app_id 出参收到的是 `project_name`（flash 真相源，不是 manifest 里那份可能被
 * 平台 Firmware.id 覆盖的 app_id）。 */
bool sdgoods_slot_read_desc(int idx, char *app_id, char *version, uint8_t *elf_sha);

/* ---- 2-4 方案 A：影子分区（「本进程内刚建出来的槽」）--------------------------
 *
 * 【它解决的是什么】2.0 的动态建槽会**运行期改写分区表**，而 IDF 的分区链表
 *   只在首次使用时加载一次、**永不重载**（`esp_partition/partition.c` 的
 *   `load_partitions()`）⇒ 新建出来的槽在**本次启动内**，链里那一条还是旧快照
 *   （2.0 预置的 `size == 0`）。`sdgoods_slot_partition()` 对 `size == 0` 一律返回
 *   NULL（那条判据本身是对的：空槽不可写），于是新槽对本进程"不存在"。
 *
 * 【为什么不能靠重读 flash 上的表来解决】那样每次查询都要读 0xC00 + 解析
 *   （`sdgoods_slot_table_t` 是 2.3 KB），而 `sdgoods_slot_partition()` 会被
 *   **逐槽循环**调用（`sdgoods_launcher_space()`、封面、`/sync`）⇒ 要么每次读几十 KB
 *   flash，要么放一个 2.3 KB 的 static（本工程 `.bss` 是硬红线，见 MEMORY.md：写一个
 *   2.7 KB 的 static 就会让 `xTaskCreate` 失败而串口毫无提示）。
 *
 * 【实际做法】建槽执行器**自己知道**新槽的 (offset, size)（`sdgoods_slot_alloc_t`
 *   里就有），写完表顺手把它们登记进来。于是既不用读表、也几乎不占 `.bss`
 *   （一个 `esp_partition_t` ≈ 48 B + 三个标量），而 address/size 与 flash 上的表
 *   **逐字一致**。`sdgoods_slot_partition()` 在链里拿到 NULL 时回落到它。
 *
 * ⚠️ **本次启动内最多只有一条影子槽**。理论上可以建第二条（调试键 `{pta…!` 手滑两次），
 *   但那样这两条里必有一条在链里看不见 ⇒ 本函数**拒绝**第二条并打日志说明，
 *   而不是悄悄把第一条顶掉（那会让"刚建好的槽突然又不存在了"变成一个无法解释的现象）。
 *
 * ⚠️ 影子分区**只能**用于读 / 写 / 擦 / 读 desc / `esp_image_verify`。
 *   **绝不能**喂给 `esp_ota_set_boot_partition()`：它先过 `esp_partition_verify()`，
 *   而那个函数按 `(flash_chip, address, size, encrypted)` 在**缓存链**里找
 *   （`partition.c:383-404`）⇒ 影子必然找不到 ⇒ `ESP_ERR_NOT_FOUND`。
 *   那正是 2-4c 要把"真正切换"推迟到重启之后的原因（见 `launch_slot` 的说明）。 */

/* 登记/注销影子槽。由建槽执行器（slot_table_flash.c）在写表成功后调用，别在别处调。
 * ⚠️ `shadow_clear()` 顺带打上"本启动内已释放"标记（见 slot_manifest.c 的 s_released_mask）：
 *    释放之后链里那条旧项**删不掉**，不加这道闸门它会被 `sdgoods_slot_partition()` 当实体
 *    交出去 ⇒ 安装链路会把刚卸载的洞当成可写目标。 */
void sdgoods_slot_shadow_set(int idx, uint32_t offset, uint32_t size);
void sdgoods_slot_shadow_clear(int idx);

/* 本进程内 `idx` 是不是"影子"（链里还是陈旧快照的那个槽）。给 launch_slot 判"现在切不过去"。 */
bool sdgoods_slot_is_shadow(int idx);

/* 本启动内 `idx` 是否被释放过（链里可能是旧实体 ⇒ 不许当分区用）。给诊断报告分标签用。 */
bool sdgoods_slot_is_released(int idx);

/* appdata 分区挂载后的根路径（高 16M 的 data/fat 分区）。 */
#define SDGOODS_APPDATA_BASE_PATH "/appdata"

/* appdata 分区的分区表标签。整表只有这一处字面量，找分区别另写字符串。 */
#define SDGOODS_APPDATA_PART_LABEL "appdata"

/* ---- appdata 下「平台自己的」子目录名（**不是 app_id**）---------------------------
 * appdata 按 app_id 隔离，但平台自己也要落少量持久状态（Wi-Fi 凭据、总开关），
 * 它们的目录名不可能等于任何 app_id。**这些名字必须在孤儿清理里白名单豁免**，
 * 否则启动器每次开机都会把平台数据当「已卸载 app 的残留」删掉
 * （2026-09-27 真机抓到：/appdata/wifi/ 被删 ⇒ 关开 Wi-Fi 后又要重新输密码）。
 * 🔴 全表只有这里一份字面量：拼路径（启动器的 app_sdk.c 会写 /appdata/wifi/…）
 *    与判白名单（slot_manifest.c）都用它。app 工程里虽然暂时没有这类目录，
 *    白名单**也必须保留**——否则将来平台级的持久状态一进 appdata，
 *    就会被开机孤儿清理当成「已卸载 app 的残留」删掉，重演同一个 bug。 */
#define SDGOODS_APPDATA_PLATFORM_DIR_WIFI "wifi"

/* 设备自助安装（推送列表）用的两个平台级目录：
 *   device/ —— 设备身份（配对拿到的 DeviceToken）与设备级配置
 *   push/   —— 平台推送下来的安装列表缓存（list.cfg）
 * 读写方是 main/sdgoods_devlink.c。**同样必须进下面的白名单**，
 * 否则开机孤儿清理会把刚拉下来的推送列表删掉 —— 表现为「每次开机都要重新下载列表」。 */
#define SDGOODS_APPDATA_PLATFORM_DIR_DEVICE "device"
#define SDGOODS_APPDATA_PLATFORM_DIR_PUSH "push"

/* 启动器主页壁纸（用户 2026-10-02 口径：只换主页背景、重启保留、
 * 壁纸按 360×360 JPEG 存、走 devlink 下载通道）。
 *   launcher/ —— 壁纸本体 wallpaper.jpg + 元信息 wallpaper.meta
 * 读写方是 main/sdgoods_wallpaper.c。**同样必须进下面的白名单**，
 * 否则每次开机孤儿清理会把用户刚设好的壁纸删掉（同一个 bug 的第三次复现）。 */
#define SDGOODS_APPDATA_PLATFORM_DIR_LAUNCHER "launcher"

/* 白名单：孤儿清理逐个跳过这些目录名。加新的平台级目录时**只改这一处**。 */
#define SDGOODS_APPDATA_RESERVED_NAMES \
    { SDGOODS_APPDATA_PLATFORM_DIR_WIFI, SDGOODS_APPDATA_PLATFORM_DIR_DEVICE, \
      SDGOODS_APPDATA_PLATFORM_DIR_PUSH, SDGOODS_APPDATA_PLATFORM_DIR_LAUNCHER }

/* 槽 manifest 持久化的 NVS namespace。 */
#define SDGOODS_SLOTS_NVS_NS "sdgoods_slots"

/* app_desc 魔数（esp_app_desc_t.magic_word）。 */
#define SDGOODS_APP_DESC_MAGIC 0xABCD5432U
