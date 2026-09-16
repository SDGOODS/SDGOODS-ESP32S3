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

#include "sdgoods_caps.h"

#include <string.h>

static sdgoods_caps_t s_caps = SDGOODS_CAP_NONE;

/* 能力位 -> 名称（与网页端 SDGOODS-CAPS: 解析约定保持一致） */
static const struct {
    uint32_t      bit;
    const char   *name;
} k_cap_names[] = {
    { SDGOODS_CAP_SCREENSHOT, "SHOT" },
};

void sdgoods_caps_add(sdgoods_caps_t caps)    { s_caps |= caps; }
void sdgoods_caps_remove(sdgoods_caps_t caps) { s_caps &= ~caps; }
sdgoods_caps_t sdgoods_board_caps(void)       { return s_caps; }
bool sdgoods_caps_has(sdgoods_caps_t cap)     { return (s_caps & cap) != 0; }

const char *sdgoods_caps_names(void)
{
    static char buf[128];
    buf[0] = '\0';
    bool first = true;
    for (size_t i = 0; i < sizeof(k_cap_names) / sizeof(k_cap_names[0]); i++) {
        if (s_caps & k_cap_names[i].bit) {
            if (!first) {
                strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
            }
            strncat(buf, k_cap_names[i].name, sizeof(buf) - strlen(buf) - 1);
            first = false;
        }
    }
    return buf;
}
