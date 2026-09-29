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

/* 白名单：孤儿清理逐个跳过这些目录名。加新的平台级目录时**只改这一处**。 */
#define SDGOODS_APPDATA_RESERVED_NAMES \
    { SDGOODS_APPDATA_PLATFORM_DIR_WIFI, SDGOODS_APPDATA_PLATFORM_DIR_DEVICE, SDGOODS_APPDATA_PLATFORM_DIR_PUSH }

/* 槽 manifest 持久化的 NVS namespace。 */
#define SDGOODS_SLOTS_NVS_NS "sdgoods_slots"

/* app_desc 魔数（esp_app_desc_t.magic_word）。 */
#define SDGOODS_APP_DESC_MAGIC 0xABCD5432U
