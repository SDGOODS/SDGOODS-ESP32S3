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
"""生成一个新应用的骨架，并自动接进构建与启动台。

一条命令搞定 3 件容易漏掉的事
----------------------------
    1. 在 main/apps/ 生成 ui_<name>.c / .h（复制 app_template.c 并改名）
    2. 把源文件插进 main/CMakeLists.txt 的 SRCS（漏了会「编译通过但功能不存在」）
    3. 把应用注册进 main/apps/apps_registry.c 的 s_apps[] 表与 include 区
       （漏了会「按钮不出现」）

用法
----
    python3 tools/new_app.py my_app "我的应用" "My App"
    python3 tools/new_app.py clock "时钟" --dry-run     # 只看会改哪些文件，不落盘

生成之后你要做的
----------------
    1. 编辑 main/apps/ui_<name>.c，把界面改成你要的样子
    2. ★ 只要出现**新的中文文案**，就重跑字体工具，否则屏上是方框（tofu）：
           python3 tools/gen_fonts.py
       然后重新编译、烧录
    3. 改完界面用截屏链路自证（电脑端运行）：
           python3 tools/screenshot_recv.py -p /dev/cu.usbmodemXXXX -o out.png -t
       -t 会自己向串口发触发命令（菜单里已经没有「截屏」按钮，串口是唯一触发方式）

想要像素风图标（像启动台上的「小鸟」「飞机」那样）
------------------------------------------
    在 main/apps/ui_app_page.c 的 icon_map_for() 里，按**图标键**加一条返回
    12x12 像素图的规则，再在 apps_registry.c 的表里填上同一个键；不加就显示文字标签。
    注意键与语言无关 —— 不要拿按钮文字当键（切到英文就查不到了）。
"""

import argparse
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS_DIR = os.path.join(ROOT, "main", "apps")
TEMPLATE_C = os.path.join(APPS_DIR, "app_template.c")
TEMPLATE_H = os.path.join(APPS_DIR, "app_template.h")
CMAKE = os.path.join(ROOT, "main", "CMakeLists.txt")
REGISTRY = os.path.join(APPS_DIR, "apps_registry.c")

CMAKE_MARK = "# >>> new_app.py: 新应用源文件插到这里 >>>"
REG_INC_MARK = "/* >>> new_app.py: 新应用 include 插到这里 >>> */"
REG_TAB_MARK = "/* >>> new_app.py: 新应用插到这里（保持缩进即可） >>> */"

TEMPLATE_TITLE = "我的应用"     # app_template.c 里预置的中文标题，会被替换成用户给的
TEMPLATE_TITLE_EN = "My App"    # 同上，英文标题（界面语言为英文时显示）


def fail(msg):
    sys.exit(f"✗ {msg}")


def insert_before_marker(path, marker, payload, what):
    """把 payload 插到 marker 那一行之前（保持 marker 在末尾，便于重复插入）。"""
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    for i, ln in enumerate(lines):
        if marker in ln:
            indent = re.match(r"\s*", ln).group(0)
            block = "".join(indent + p + "\n" for p in payload)
            lines.insert(i, block)
            with open(path, "w", encoding="utf-8") as f:
                f.writelines(lines)
            print(f"  ✓ {what}: {os.path.relpath(path, ROOT)}")
            return True
    fail(f"在 {os.path.relpath(path, ROOT)} 里找不到插入标记 `{marker}`。\n"
         f"  这个标记是给脚本定位用的，不要删。若确实被删了，请手动把下面几行加上：\n"
         f"    {payload}")


def main():
    ap = argparse.ArgumentParser(description="生成一个新应用的骨架并自动接线")
    ap.add_argument("name", help="应用标识（小写字母/数字/下划线，如 my_app）")
    ap.add_argument("title", help="启动台按钮上的中文文字（中文会占用字体子集）")
    ap.add_argument("title_en", nargs="?", default=None,
                    help="启动台按钮上的英文文字（界面语言为英文时显示）；省略则与中文相同")
    ap.add_argument("--dry-run", action="store_true", help="只显示会做什么，不写文件")
    ap.add_argument("--force", action="store_true", help="覆盖已存在的同名文件")
    args = ap.parse_args()
    if not args.title_en:
        args.title_en = args.title   # 没给英文名就用中文名（英文界面下会显示中文，可后续再补）

    name = args.name.strip().lower()
    name = re.sub(r"^ui_", "", name)          # 允许用户写 ui_xxx，避免出现 ui_ui_xxx
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        fail(f"应用标识不合法: {args.name!r}（只允许小写字母、数字、下划线，且以字母开头）")
    if os.path.basename(name) != name:
        fail("应用标识不能包含路径分隔符")

    out_c = os.path.join(APPS_DIR, f"ui_{name}.c")
    out_h = os.path.join(APPS_DIR, f"ui_{name}.h")
    if not args.force:
        for p in (out_c, out_h):
            if os.path.exists(p):
                fail(f"{os.path.relpath(p, ROOT)} 已存在（要覆盖请加 --force）")

    for p in (TEMPLATE_C, TEMPLATE_H, CMAKE, REGISTRY):
        if not os.path.isfile(p):
            fail(f"找不到 {os.path.relpath(p, ROOT)}")

    # 模板 → 新应用：改名、改函数名、改标题
    def render(text):
        text = text.replace('#include "app_template.h"', f'#include "ui_{name}.h"')
        text = text.replace("APP_TEMPLATE", name.upper())
        text = text.replace("app_template", name)      # ← 同时把 ui_app_template_show 变成 ui_<name>_show
        # 先替换英文（"My App" 不是 "我的应用" 的子串，顺序其实无所谓，这里保持显式）
        text = text.replace(TEMPLATE_TITLE_EN, args.title_en)
        text = text.replace(TEMPLATE_TITLE, args.title)
        return text

    src_c = render(open(TEMPLATE_C, encoding="utf-8").read())
    src_h = render(open(TEMPLATE_H, encoding="utf-8").read())

    print(f"应用: {name}   按钮: {args.title} / {args.title_en}")
    if args.dry_run:
        print("  [dry-run] 会创建:")
        print(f"    {os.path.relpath(out_c, ROOT)}")
        print(f"    {os.path.relpath(out_h, ROOT)}")
        print("  [dry-run] 会修改:")
        print(f"    {os.path.relpath(CMAKE, ROOT)}   （SRCS 加 apps/ui_{name}.c）")
        print(f"    {os.path.relpath(REGISTRY, ROOT)}（include + s_apps[] 各加一行）")
        return

    with open(out_c, "w", encoding="utf-8") as f:
        f.write(src_c)
    with open(out_h, "w", encoding="utf-8") as f:
        f.write(src_h)
    print(f"  ✓ 生成: {os.path.relpath(out_c, ROOT)}")
    print(f"  ✓ 生成: {os.path.relpath(out_h, ROOT)}")

    insert_before_marker(CMAKE, CMAKE_MARK, [f'"apps/ui_{name}.c"'], "源文件加入构建")
    insert_before_marker(REGISTRY, REG_INC_MARK, [f'#include "ui_{name}.h"'], "注册 include")
    insert_before_marker(
        REGISTRY, REG_TAB_MARK,
        [f'{{ .label_zh = "{args.title}", .label_en = "{args.title_en}", .icon = NULL,',
         f'  .show = ui_{name}_show, .poll = ui_{name}_poll }},'],
        "注册到启动台",
    )

    print(f"""
完成。接下来：

  1. 编辑 {os.path.relpath(out_c, ROOT)}，把界面改成你要的样子
     · 界面栅格用 SDG_UI_* 常量（见 platform 的 sdgoods_ui.h）
     · 顶部下滑菜单 / 退出 / 暂停 已由 ui_app_shell 自动接好
     · 界面文案用 SDG_T("中文", "English")，默认英文显示（见 sdgoods_i18n.h）

  2. ★ 如果加了新的中文文案，必须重新生成字体子集，否则屏上是方框：
       python3 tools/gen_fonts.py
     （首次需要先跑 python3 tools/fetch_fonts.py 下载 OFL 源字体）

  3. 编译烧录：
       idf.py -B build build flash

  4. 改完用截屏自证（串口触发；菜单里已无「截屏」按钮）：
       python3 tools/screenshot_recv.py -p /dev/cu.usbmodemXXXX -o out.png -t
""")


if __name__ == "__main__":
    main()
