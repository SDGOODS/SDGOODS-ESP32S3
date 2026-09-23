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

/* 槽 manifest 持久化的 NVS namespace。 */
#define SDGOODS_SLOTS_NVS_NS "sdgoods_slots"

/* app_desc 魔数（esp_app_desc_t.magic_word）。 */
#define SDGOODS_APP_DESC_MAGIC 0xABCD5432U
