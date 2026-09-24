#!/usr/bin/env python3
# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程 · 开发工具
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
# 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
#   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
"""把已下载的 LVGL 组件打成离线包，供国内用户跳过组件管理器的联网下载。

为什么需要这一步
----------------
首次编译会由 ESP 组件管理器从 `components.espressif.com` 拉取 LVGL（约 98MB）。
该域名是乐鑫自有服务，不走 GitHub，但国内偶发超时，且 98MB 重下一次很痛。

组件管理器在下载前会先看 `managed_components/lvgl__lvgl/` 是否已存在，并比对
`dependencies.lock` 里的 component_hash：一致就跳过下载。所以只要把目录原样
搬过去即可离线，不需要任何特殊格式。

本脚本产出的 tar 包内顶层路径就是 `managed_components/lvgl__lvgl/`，用户直接
在工程根目录解压即可。

用法
----
    python3 tools/pack_lvgl_offline.py                  # 产出 dist/lvgl__lvgl_<ver>.tar.gz
    python3 tools/pack_lvgl_offline.py --out /tmp/x.tar.gz

产出物放自己的网站/内网，国内用户：

    tar xzf lvgl__lvgl_<ver>.tar.gz -C <工程目录>

补丁说明：本工程对 LVGL 的 GIF 解码器有本地补丁（main/patches/），每次 CMake
configure 会把这三个文件**无条件覆盖**成补丁版本。所以离线包里带不带补丁都不影响，
用户解压后 configure 会自动对齐。（在一台编译过的机器上打包，包里就是带补丁的。）
"""

import argparse
import hashlib
import os
import re
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPONENT_DIR = os.path.join(ROOT, "managed_components", "lvgl__lvgl")
LOCK_FILE = os.path.join(ROOT, "dependencies.lock")
DEFAULT_OUT_DIR = os.path.join(ROOT, "dist")

# 打进包里反而添乱的东西
EXCLUDE_DIRS = {".git", "__pycache__"}


def read_lock_version():
    """从 dependencies.lock 取 lvgl/lvgl 的版本号（不引 pyyaml 依赖）。"""
    if not os.path.isfile(LOCK_FILE):
        return None
    with open(LOCK_FILE, encoding="utf-8") as f:
        lines = f.read().splitlines()

    in_lvgl = False
    for line in lines:
        if re.match(r"^  lvgl/lvgl:\s*$", line):
            in_lvgl = True
            continue
        if not in_lvgl:
            continue
        m = re.match(r"^\s+version:\s*(\S+)", line)
        if m:
            return m.group(1)
        if re.match(r"^  \S", line):  # 缩进回到顶层 key，说明没找到
            return None
    return None


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_filter():
    def _filter(info):
        base = os.path.basename(info.name)
        if base in EXCLUDE_DIRS:
            return None
        if base.endswith(".pyc"):
            return None
        return info

    return _filter


def main():
    parser = argparse.ArgumentParser(description="打包 LVGL 托管组件为离线包")
    parser.add_argument("--out", help="输出 tar.gz 路径（默认 dist/lvgl__lvgl_<ver>.tar.gz）")
    args = parser.parse_args()

    if not os.path.isdir(COMPONENT_DIR):
        print("[pack-lvgl] 找不到 %s" % COMPONENT_DIR)
        print("[pack-lvgl] 请先完整编译一次（组件管理器会自动下载），再运行本脚本。")
        return 1

    version = read_lock_version() or "unknown"
    out = args.out or os.path.join(DEFAULT_OUT_DIR, "lvgl__lvgl_%s.tar.gz" % version)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)

    arc_root = os.path.join("managed_components", "lvgl__lvgl")
    file_count = 0
    with tarfile.open(out, "w:gz") as tar:
        tar.add(COMPONENT_DIR, arcname=arc_root, filter=build_filter())
        file_count = len(tar.getmembers())

    size_mb = os.path.getsize(out) / 1024 / 1024
    print("[pack-lvgl] LVGL 版本      : %s" % version)
    print("[pack-lvgl] 文件数          : %d" % file_count)
    print("[pack-lvgl] 输出            : %s" % out)
    print("[pack-lvgl] 体积            : %.1f MB" % size_mb)
    print("[pack-lvgl] sha256          : %s" % sha256_of(out))
    print("")
    print("[pack-lvgl] 把这个包放到自己的网站/内网，国内用户执行：")
    print("             tar xzf %s -C <工程目录>" % os.path.basename(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
