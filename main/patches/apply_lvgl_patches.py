#!/usr/bin/env python3
"""把 main/patches/ 下的补丁文件应用到 managed_components（幂等 + 逐字节校验）。

为什么需要这一步
----------------
`managed_components/` 是 ESP-IDF 组件管理器下载的第三方代码，**不入库**
（见 .gitignore）。但本项目的开机动画依赖对 LVGL 内置 GIF 解码器的改动：

    360×360 的 GIF canvas 在 16bpp 下约 518KB，远超 LVGL 内部内存池，
    因此必须把 canvas 放到 PSRAM，并把 GIF 源数据从 flash 拷到 RAM 加速解码。

这处改动横跨 3 个文件（gifdec.c / gifdec.h / lv_gif.c），**必须成套替换**：
只改一半会直接编译失败（`too few arguments to function 'gd_open_gif_data'`）
或者画面颜色发花。如果补丁不随源码走，别人 clone 后可能编译不过，
或者编译过了但开机动画在运行时分配失败 —— 都很难查。

所以补丁文件放在 `main/patches/` 随源码提交，由 `main/CMakeLists.txt` 在每次
CMake configure 阶段调用本脚本覆盖过去。

行为
----
- 幂等：目标文件与补丁源**逐字节相同**就跳过。
- 组件目录还没下载时只警告、不中断（首次 configure 时管理器可能尚未落盘，
  下一次 configure 会补上）。
- 应用后逐字节比对；不一致则退出码非零，configure 会因此 FATAL_ERROR。
  宁可编译期报错，也不要产出一个开机动画会崩的固件。
"""

import filecmp
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PATCH_DIR = os.path.join(ROOT, "main", "patches")

# 补丁目录名 → [(文件名, 目标相对 managed_components 的路径), ...]
# 注意：这些文件是一套的（签名、结构体、调用点互相依赖），不要只应用其中一部分。
PATCHES = {
    "lvgl-8.3.11-gif-psram": [
        ("gifdec.c", "lvgl__lvgl/src/extra/libs/gif/gifdec.c"),
        ("gifdec.h", "lvgl__lvgl/src/extra/libs/gif/gifdec.h"),
        ("lv_gif.c", "lvgl__lvgl/src/extra/libs/gif/lv_gif.c"),
    ],
}


def main():
    comp_root = os.path.join(ROOT, "managed_components")
    if not os.path.isdir(comp_root):
        print("[lvgl-patch] managed_components/ 尚不存在，跳过（下一次 configure 会补上）")
        return 0

    applied, uptodate, missing, mismatch = [], [], [], []

    for patch_name, files in PATCHES.items():
        for filename, dst_rel in files:
            src = os.path.join(PATCH_DIR, patch_name, filename)
            dst = os.path.join(comp_root, dst_rel)

            if not os.path.isfile(src):
                print(f"[lvgl-patch] 错误: 缺少补丁源文件 {src}", file=sys.stderr)
                return 1
            if not os.path.isfile(dst):
                missing.append(dst_rel)
                continue

            if filecmp.cmp(src, dst, shallow=False):
                uptodate.append(dst_rel)
                continue

            shutil.copyfile(src, dst)
            if filecmp.cmp(src, dst, shallow=False):
                applied.append(dst_rel)
            else:
                mismatch.append(dst_rel)

    if missing:
        print(f"[lvgl-patch] 警告: 目标缺失，跳过 {', '.join(missing)}"
              f"（LVGL 版本变了？见 main/patches/README.md）")
    if applied:
        print(f"[lvgl-patch] 已应用: {', '.join(applied)}")
    if uptodate:
        print(f"[lvgl-patch] 已是最新: {', '.join(uptodate)}")

    # 最终校验：这几个文件必须与补丁源逐字节一致
    for patch_name, files in PATCHES.items():
        for filename, dst_rel in files:
            src = os.path.join(PATCH_DIR, patch_name, filename)
            dst = os.path.join(comp_root, dst_rel)
            if os.path.isfile(dst) and not filecmp.cmp(src, dst, shallow=False):
                print(f"[lvgl-patch] 错误: {dst_rel} 与补丁源不一致", file=sys.stderr)
                return 1

    if mismatch:
        print(f"[lvgl-patch] 错误: 写入后校验失败 {', '.join(mismatch)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
