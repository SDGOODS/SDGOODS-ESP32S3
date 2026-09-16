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
"""
字体度量：离线核对「换了字体/加了文案之后，文字会不会溢出或变成方框」。

为什么需要它：设备端取画面只有「串口 's' 触发截屏」一条路（菜单里的截屏按钮已移除），
手势到不了的界面（例如启动台、菜单）没法点进去截图；而且单张 360×360 截屏走
USB-Serial-JTAG 要约 33 秒，试错成本很高。**文本宽度可以直接从字体表算出来**，
所以先在电脑上算一遍，比烧板子快得多。

做法：解析 lv_font_conv 生成的 .c 文件里的 glyph_dsc（前进宽度，单位 1/16 px）
与 cmaps（码点 → glyph id），得到「码点 → 宽度(px)」表，然后逐字累加。

    python3 tools/font_metrics.py                    # 用内置的本项目关键文案表
    python3 tools/font_metrics.py --strings "谷仓共创计划,按电源键返回"
    python3 tools/font_metrics.py --old <git-ref>    # 与某个历史版本的字体对比宽度

★ 圆屏的「可用宽度」不是常数
--------------------------------
屏幕是**圆**的（直径 360），同一行文字在越靠上/下的位置，能放下的宽度越窄：

    可用宽度 ≈ 2 * sqrt(180² - dy²)      dy = 该行离屏幕中心的最远距离

举例：屏幕中心（y=180）能放满 360px；而关于页的许可标签在 y=232，只剩约 322px。
文案表里凡是**圆屏上的整行文本**都带 y 坐标，工具会按弦宽自动收紧上限 ——
只写死一个 340px 会在这种位置上给出「OK」的错误结论。
按钮类文案不受此限（按钮自己是个圆，用直径 76 判）。

退出码：0 = 全部通过；1 = 有缺字或超宽。
"""
import argparse
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "components", "sdgoods_board", "fonts")
# 重构前后字体所在路径不同，两处都试
NEW_REL = "components/sdgoods_board/fonts"
OLD_REL_CANDIDATES = ("components/sdgoods_board/fonts", "main")

LCD_SIZE = 360          # 圆形屏直径（也是 LVGL 横竖向分辨率）
BTN_SIZE = 76           # SDG_UI_BTN_SIZE：圆按钮直径

# 本项目真实文案表。
#   字段：说明, 字符串, 字号, 宽度上限(px), y 坐标, 字体名, 折行宽度(px)
#     · limit：容器硬上限。圆按钮写 76；整行文本写 0 表示「由 y 算弦宽」，
#              写其它值表示「弦宽与它取较小者」。
#     · y    ：该行的 y 坐标（LV_ALIGN_TOP_MID 的偏移）。None = 不受圆屏限制。
#     · font ：实际渲染用的字体。None = 按字号默认顺序（si_yuan 主字体 + cn_font 兜底），
#              只有「代码里直接指定了 cn_font_*」的地方才需要显式写（如应用外壳菜单）。
#     · wrap ：该 label 的限宽（lv_label_set_long_mode WRAP + lv_obj_set_width）。
#              None = 单行不折。设了以后**按折行后的每一行**分别判宽度，
#              并额外检查「折行块本身（宽 wrap）能不能放进圆屏的弦宽」。
CASES = [
    # ---------------- 主页（ui_home.c）----------------
    # y 用 SDG_UI_TITLE_Y(=35)，与 DEMO / 应用页标题同高
    ("主页标题",   "主页",           16, 0, 35, None, None),
    ("主页标题",   "HOME",           16, 0, 35, None, None),
    # 主页版本号已按要求移除（版本只出现在关于页）；主页顶部只剩产品名
    ("主页按钮",   "DEMO",           14, BTN_SIZE, None, None, None),
    ("主页按钮",   "应用",           14, BTN_SIZE, None, None, None),
    ("主页按钮",   "Apps",           14, BTN_SIZE, None, None, None),
    ("主页按钮",   "关机",           14, BTN_SIZE, None, None, None),
    ("主页按钮",   "Power off",      14, BTN_SIZE, None, None, None),
    ("主页页脚",   "谷仓SDGOODS",    14, 0, 296, None, None),
    # ---------------- 启动台（ui_app_page.c）----------------
    ("启动台标题", "应用",           16, 0, 35, None, None),
    ("启动台标题", "Apps",           16, 0, 35, None, None),
    ("启动台提示", "按电源键返回",   14, 0, 296, None, None),
    ("启动台提示", "Power key to go back", 14, 0, 296, None, None),
    ("启动台按钮", "小鸟",           14, BTN_SIZE, None, None, None),
    ("启动台按钮", "Bird",           14, BTN_SIZE, None, None, None),
    ("启动台按钮", "飞机",           14, BTN_SIZE, None, None, None),
    ("启动台按钮", "Plane",          14, BTN_SIZE, None, None, None),
    # ---------------- DEMO 页（ui_demo_page.c）----------------
    ("DEMO 按钮",  "录音",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "Rec",            14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "WIFI",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "蓝牙",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "BLE",            14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "其他",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "More",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "关于",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "About",          14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "中文",           14, BTN_SIZE, None, None, None),
    ("DEMO 按钮",  "English",        14, BTN_SIZE, None, None, None),
    # ---------------- 关于页（ui_about.c，y 见 ABT_Y_*）----------------
    ("关于页",     "关于",                               14, 0, 44,  None, None),
    ("关于页",     "About",                              14, 0, 44,  None, None),
    ("关于页",     "谷仓共创计划",                       14, 0, 72,  None, None),
    ("关于页",     "谷仓 SDGOODS 开放平台",              14, 0, 100, None, None),
    ("关于页",     "谷仓次元屏（谷仓电子徽章）",         16, 0, 130, None, None),
    ("关于页",     "深圳希德创新网络有限公司 (SDGOODS)", 14, 0, 170, None, None),
    # 联系信息已按产品口径从关于页移除（y=196 那行取消），下面的行整体上移
    ("关于页",     "固件版本 09160047",                  14, 0, 200, None, None),
    ("关于页",     "Firmware 09160047",                  14, 0, 200, None, None),
    # 许可标签：关于页是限宽折行显示的（ABT_LICENSE_W=200）——
    # limit 走弦宽（该行 y=254 处只剩约 291px），wrap=200 才是折行宽度
    ("关于页·许可", "个人免费 · 商用需授权",             14, 0, 232, None, 200),
    ("关于页·许可", "Free for personal use · Commercial needs license", 14, 0, 232, None, 200),
    ("关于页",     "按电源键返回",                       14, 0, 296, None, None),
    ("关于页",     "Power key to go back",               14, 0, 296, None, None),
    # ---------------- 应用外壳菜单（sdgoods_app_shell.c，直接用 cn_font_*）----
    ("外壳菜单",   "菜单",           16, 0, 44, "cn_font_16", None),
    ("外壳菜单",   "Menu",           16, 0, 44, "cn_font_16", None),
    ("外壳菜单",   "音量: 100%",     14, 0, 82, "cn_font_14", None),
    ("外壳菜单",   "Vol: 100%",      14, 0, 82, "cn_font_14", None),
    ("外壳菜单",   "音量+",          14, BTN_SIZE, None, None, None),
    ("外壳菜单",   "音量-",          14, BTN_SIZE, None, None, None),
    ("外壳菜单",   "Vol+",           14, BTN_SIZE, None, None, None),
    ("外壳菜单",   "Vol-",           14, BTN_SIZE, None, None, None),
    ("外壳菜单",   "退出",           14, BTN_SIZE, None, None, None),
    ("外壳菜单",   "Exit",           14, BTN_SIZE, None, None, None),
]

NAMES = ("cn_font_14", "cn_font_16",
         "si_yuan_black_icon_14", "si_yuan_black_icon_16")


def circle_chord(y, h):
    """圆屏上「行顶 y、行高 h」这一行能放下的最大横向宽度（弦长）。

    圆心在 (180, 180)，半径 180。取行内离圆心最远的那个纵向距离 dy，
    弦长 = 2*sqrt(r^2 - dy^2)。行高按实际字形高度估，宁严勿松。
    """
    r = LCD_SIZE / 2.0
    cy = LCD_SIZE / 2.0
    dy = max(abs(y - cy), abs(y + h - cy))
    if dy >= r:
        return 0.0
    return 2.0 * math.sqrt(r * r - dy * dy)


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


def maps_for(fonts, size, font_key=None):
    if font_key:
        return [fonts.get(font_key, {})]
    if size == 14:
        order = ("si_yuan_black_icon_14", "cn_font_14")
    else:
        order = ("si_yuan_black_icon_16", "cn_font_16")
    return [fonts.get(k, {}) for k in order]


def _tokens(s):
    """切出「可断行的词」：拉丁按空格分，每个词带上它后面的空格。"""
    return re.findall(r"\S+\s*", s)


def wrap_lines(s, width, maps, max_lines=8):
    """模拟 LVGL 的自动折行，返回每一行文本。

    局限：CJK 长串（中间没空格）会被当成一个整词，超宽也不会断 ——
    这属于「宁严勿松」：真超了会报超宽让你改文案，而不是漏判放过。
    """
    lines, cur = [], ""
    for w in _tokens(s):
        cand = cur + w
        if cur and measure(cand, maps)[0] > width:
            lines.append(cur.rstrip())
            cur = w.lstrip()
            if len(lines) >= max_lines:
                break
        else:
            cur = cand
    if cur.strip() and len(lines) < max_lines:
        lines.append(cur.rstrip())
    return lines or [s]


def effective_limit(limit, y, size):
    """容器上限与圆屏弦宽取较小者，返回 (上限, 说明)。"""
    if y is None:
        return float(limit), "容器"
    # 行高：LVGL 14px 字体的 line_height 实测 16，16px 字体 18；再留 2px 余量
    chord = circle_chord(y, size + 3)
    if limit and limit > 0:
        if limit <= chord:
            return float(limit), "容器"
        return chord, "弦宽"
    return chord, "弦宽"


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
        cases = [("自定义", x.strip(), args.size, args.limit, None, None, None)
                 for x in args.strings.split(",") if x.strip()]
    else:
        cases = CASES

    have_old = bool(old)
    head = f"{'场景':<12}{'字符串':<26}{'字号':<5}{'上限':>6}{'依据':>5}"
    head += f"{'旧宽':>8}{'新宽':>8}{'变化':>9}  判定" if have_old else f"{'宽度':>8}  判定"
    print("=" * 96)
    print(head)
    print("=" * 96)

    bad = 0
    wrap_notes = []
    for scene, txt, size, limit, y, font_key, wrap in cases:
        lim, basis = effective_limit(limit, y, size)
        maps = maps_for(new, size, font_key)
        nw, nmiss = measure(txt, maps)
        nline = 1
        if wrap:
            lines_all = wrap_lines(txt, wrap, maps)
            nline = len(lines_all)
            nw = max(measure(l, maps)[0] for l in lines_all)
            # 折行块整体（宽 wrap）也得放得进可用宽度，否则连块都摆不下
            if wrap > lim:
                basis = "折行超限"
            else:
                lim = min(lim, float(wrap))
                basis = "折行"
            wrap_notes.append((scene, txt, lines_all))
        if have_old:
            ow, _ = measure(txt, maps_for(old, size, font_key))
            cols = f"{ow:>8.1f}{nw:>8.1f}{nw - ow:>+9.1f}"
        else:
            cols = f"{nw:>8.1f}"
        show = txt if len(txt) <= 24 else txt[:23] + "…"
        if wrap and nline > 1:
            show = "%s(%d行)" % (show, nline)
        if nmiss:
            verdict = f"⚠ 缺字: {''.join(nmiss)}"
            bad += 1
        elif nw > lim:
            verdict = f"⚠ 超宽(>{lim:.0f} {basis})"
            bad += 1
        else:
            verdict = "OK"
        print(f"{scene:<12}{show:<26}{size:<5}{lim:>6.0f}{basis:>5}{cols}  {verdict}")

    print("=" * 96)
    if have_old:
        print(f"对比基准：{ref[:8]} 的字体")
    else:
        print("提示：没取到历史字体，只报当前宽度（可用 --old <ref> 指定）")
    if wrap_notes:
        print("限宽折行的实际断行（LVGL 在空格处断；CJK 长串不断）：")
        for scene, t, lines_all in wrap_notes:
            for i, l in enumerate(lines_all, 1):
                print("  %-11s 第%d行 %6.1fpx  %s"
                      % (scene, i, measure(l, maps_for(new, 14))[0], l))
    print("说明：『上限』列对圆屏整行文本是**按 y 算出的弦宽** —— 离屏幕中心越远越窄；")
    print("      带『折行』的按换行后最宽的那一行判，并检查折行块本身能否放进弦宽。")
    print("结论：" + ("全部通过" if bad == 0 else f"{bad} 项需注意"))
    if bad:
        print("  · 缺字 → 跑 tools/gen_fonts.py 重新生成")
        print("  · 超宽 → 缩小字号、缩短文案，或像关于页那样限宽折行（lv_label_set_long_mode WRAP）")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
