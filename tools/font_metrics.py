#!/usr/bin/env python3
# SDGOODS 开放平台基础工程 · 开发工具
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
# -*- coding: utf-8 -*-
"""
字体度量：离线核对「换了字体/加了文案之后，文字会不会溢出或变成方框」。

为什么需要它：设备端只有「顶部下滑 → 截屏」这一条取画面的路（串口命令只有 's'），
手势到不了的界面（例如启动台、菜单）没法直接截图；但**文本宽度可以直接从字体表算出来**。
换字体后最需要确认的就是「某个 label 是不是会超出按钮/圆屏」。

做法：解析 lv_font_conv 生成的 .c 文件里的 glyph_dsc（前进宽度，单位 1/16 px）
与 cmaps（码点 → glyph id），得到「码点 → 宽度(px)」表，然后逐字累加。

    python3 tools/font_metrics.py                    # 用内置的本项目关键文案表
    python3 tools/font_metrics.py --strings "俄罗斯方块,按电源键返回"
    python3 tools/font_metrics.py --old <git-ref>    # 与某个历史版本的字体对比宽度

判定依据（来自源码实测）：
  * 启动台按钮标签 / 主页按钮 → si_yuan_black_icon_14（fallback cn_font_14），圆按钮直径 76px
  * 标题 / 大号提示          → si_yuan_black_icon_16（fallback cn_font_16）
  * 游戏页                   → cn_font_14 / cn_font_16（全量集）
注意：CJK 字形的前进宽度在「思源系」与「Arial Unicode」之间是**一样的**（都以全角 em 为基准），
      所以换字体不会改变中文排版，只有拉丁字母的宽度会有零点几像素的差别。

退出码：0 = 全部通过；1 = 有缺字或超宽。
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "components", "sdgoods_board", "fonts")
# 重构前后字体所在路径不同，两处都试
NEW_REL = "components/sdgoods_board/fonts"
OLD_REL_CANDIDATES = ("components/sdgoods_board/fonts", "main")

# 本项目真实文案表：(说明, 字符串, 字号, 容器宽度上限 px)
# 上限依据：圆按钮直径 76（ui_app_page.c 的 SDG_UI_BTN_SIZE）；整行文本按 340（圆屏可用宽度）
CASES = [
    ("启动台按钮", "小鸟",        14, 76),
    ("启动台按钮", "飞机",        14, 76),
    ("启动台按钮", "俄罗斯方块",  14, 76),
    ("启动台按钮", "贪吃蛇",      14, 76),
    ("启动台按钮", "示例",        14, 76),
    ("主页按钮",   "DEMO",        14, 76),
    ("主页按钮",   "应用",        14, 76),
    ("主页按钮",   "关机",        14, 76),
    ("主页页脚",   "谷仓SDGOODS", 14, 340),
    ("主页标题",   "谷仓电子徽章", 16, 340),
    ("启动台标题", "应用",        16, 340),
    ("启动台提示", "按电源键返回", 16, 340),
    ("外壳菜单",   "音量: 100%",   14, 340),
    ("外壳菜单",   "退出",        14, 340),
]

NAMES = ("cn_font_14", "cn_font_16",
         "si_yuan_black_icon_14", "si_yuan_black_icon_16")


def parse_adv(src):
    """从字体 .c 源码解析出 {码点: 前进宽度(px)}。"""
    m = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise ValueError("找不到 glyph_dsc")
    advs = [int(x) / 16.0 for x in re.findall(r"\.adv_w\s*=\s*(\d+)", m.group(1))]

    m = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise ValueError("找不到 cmaps")
    entries = re.findall(r"\{([^{}]*range_start[^{}]*)\}", m.group(1), re.S)
    if not entries:
        raise ValueError("cmaps 里没有条目")

    out = {}
    for e in entries:
        rs = int(re.search(r"\.range_start\s*=\s*(\d+)", e).group(1))
        rl = int(re.search(r"\.range_length\s*=\s*(\d+)", e).group(1))
        gs = int(re.search(r"\.glyph_id_start\s*=\s*(\d+)", e).group(1))
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", e).group(1)
        typ = re.search(r"\.type\s*=\s*(\w+)", e).group(1)
        if "SPARSE" in typ:
            # 稀疏表：码点 = range_start + unicode_list[i]（存的是相对偏移）
            m2 = re.search(rf"{ul}\[\]\s*=\s*\{{(.*?)\}}", src, re.S)
            if not m2:
                raise ValueError(f"找不到 {ul}")
            for i, off in enumerate(re.findall(r"0x[0-9a-fA-F]+", m2.group(1))):
                out[rs + int(off, 16)] = advs[gs + i]
        else:
            for i in range(rl):
                out[rs + i] = advs[gs + i]
    return out


def load_new():
    fonts = {}
    for n in NAMES:
        p = os.path.join(FONT_DIR, n + ".c")
        if not os.path.exists(p):
            print(f"!! 缺少 {p}", file=sys.stderr)
            continue
        fonts[n] = parse_adv(open(p, encoding="utf-8").read())
    return fonts


def load_old(ref):
    """从 git 历史里取旧字体；取不到就返回 {}。"""
    fonts = {}
    for n in NAMES:
        for rel in OLD_REL_CANDIDATES:
            try:
                txt = subprocess.run(
                    ["git", "-C", ROOT, "show", f"{ref}:{rel}/{n}.c"],
                    capture_output=True, text=True, check=True).stdout
            except subprocess.CalledProcessError:
                continue
            try:
                fonts[n] = parse_adv(txt)
            except ValueError:
                continue
            break
    return fonts


def measure(s, maps):
    total, missing = 0.0, []
    for ch in s:
        for mp in maps:
            if ord(ch) in mp:
                total += mp[ord(ch)]
                break
        else:
            missing.append(ch)
    return total, missing


def maps_for(fonts, size):
    if size == 14:
        order = ("si_yuan_black_icon_14", "cn_font_14")
    else:
        order = ("si_yuan_black_icon_16", "cn_font_16")
    return [fonts.get(k, {}) for k in order]


def main():
    ap = argparse.ArgumentParser(description="离线核对文本宽度与字体覆盖")
    ap.add_argument("--strings", help="逗号分隔的自定义字符串（默认用内置文案表）")
    ap.add_argument("--size", type=int, default=14, choices=(14, 16),
                    help="--strings 的字号（默认 14）")
    ap.add_argument("--limit", type=int, default=340, help="--strings 的宽度上限（默认 340）")
    ap.add_argument("--old", metavar="REF",
                    help="与某个 git ref 的字体对比（默认取仓库首个提交）")
    args = ap.parse_args()

    new = load_new()
    if not new:
        print("没有可用的字体文件，先在 main/ 与 components/ 下找 *.c", file=sys.stderr)
        return 1

    ref = args.old
    if ref is None:
        try:
            ref = subprocess.run(["git", "-C", ROOT, "rev-list", "--max-parents=0", "HEAD"],
                                 capture_output=True, text=True, check=True).stdout.split()[0]
        except (subprocess.CalledProcessError, IndexError):
            ref = None
    old = load_old(ref) if ref else {}

    if args.strings:
        cases = [("自定义", s.strip(), args.size, args.limit)
                 for s in args.strings.split(",") if s.strip()]
    else:
        cases = CASES

    have_old = bool(old)
    head = f"{'场景':<10}{'字符串':<14}{'字号':<5}"
    head += f"{'旧宽':>8}{'新宽':>8}{'变化':>9}  判定" if have_old else f"{'宽度':>8}  判定"
    print("=" * 78)
    print(head)
    print("=" * 78)

    bad = 0
    for scene, s, size, limit in cases:
        nw, nmiss = measure(s, maps_for(new, size))
        if have_old:
            ow, _ = measure(s, maps_for(old, size))
            cols = f"{ow:>8.1f}{nw:>8.1f}{nw - ow:>+9.1f}"
        else:
            cols = f"{nw:>8.1f}"
        if nmiss:
            verdict = f"⚠ 缺字: {''.join(nmiss)}"
            bad += 1
        elif nw > limit:
            verdict = f"⚠ 超宽(>{limit})"
            bad += 1
        else:
            verdict = "OK"
        print(f"{scene:<10}{s:<14}{size:<5}{cols}  {verdict}")

    print("=" * 78)
    if have_old:
        print(f"对比基准：{ref[:8]} 的字体")
    else:
        print("提示：没取到历史字体，只报当前宽度（可用 --old <ref> 指定）")
    print("结论：" + ("全部通过" if bad == 0 else f"{bad} 项需注意"))
    if bad:
        print("  · 缺字 → 跑 tools/gen_fonts.py 重新生成")
        print("  · 超宽 → 改用稍小字号，或缩短文案")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
