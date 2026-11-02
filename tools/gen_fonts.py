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
"""重新生成 LVGL 中文字体子集（SIL OFL 1.1 授权）。

为什么需要子集字体
------------------
固件 flash 只有几 MB，塞不下整套中文字库（20MB+）。所以项目用「子集字体」：
扫描源码里实际出现的字符，只把这些字形烘进 .c 字体文件。
好处是字体只占几百 KB；代价是 —— ★ **新增中文文案后必须重跑本脚本** ★，
否则新字在屏上是方框（tofu）。

生成两套字体
------------
    cn_font_14 / cn_font_16
        全量子集：源码里扫描到的**所有**字符。作为兜底（fallback）。
    si_yuan_black_icon_14 / _16
        UI 精选子集：日常界面/菜单用的那批字。作为**主字体**，
        它的 .fallback 在编译期指向 cn_font_*，精选集里没有的字自动回退。

界面代码用 si_yuan_black_icon_*；遇到精选集外的字由 LVGL 自动查 cn_font_*。

源字体
------
默认用 **Noto Sans SC**（SIL OFL 1.1，允许嵌入到软件中再分发、含商用）。
首次使用先跑 tools/fetch_fonts.py 把源字体下载到 tools/fonts/。
换别的字体：--font <path>（记得确认对方的再分发授权）。

用法
----
    python3 tools/fetch_fonts.py            # 首次：下载源字体
    python3 tools/gen_fonts.py              # 重新生成全部字体（14/16 号）
    python3 tools/gen_fonts.py --sizes 14,16,20
    python3 tools/gen_fonts.py --check      # 只校验当前字体是否缺字，不重新生成

依赖
----
    npm i lv_font_conv        # 官方字体转换工具
    python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv

可移植性
--------
lv_font_conv 的 `--format lvgl` 会把**完整命令行**写进产物头部的 `Opts:` 注释，
于是 `--font` / `-o` 里的本机绝对路径会随源码一起进仓库。本脚本在写盘前
把这两个路径参数归一化（仓库内 → 相对仓库根；仓库外 → `<abs>/文件名`），
并做一次守卫：产物里若仍出现本机路径就直接报错，而不是静默泄漏。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS_DIR = os.path.join(ROOT, "components", "sdgoods_board", "fonts")
# 扫描这些目录下的源码，收集用到的字符
SCAN_DIRS = [
    os.path.join(ROOT, "main"),
    os.path.join(ROOT, "components", "sdgoods_board"),
    # 🔴 2026-10-10 补上：**控制中心（sdgoods_cc.c）与 app SDK 在这一层**，
    #    而它恰恰是全工程中文文案**最多**的一个文件（设置 / 网络 / 设备 / 语言 / 关于
    #    各页的标题、按钮、提示全在里面）。漏扫它的后果不是"少收几个字"：
    #      · 常见字（重 / 置 / 删 / 除…）因为在 main/ 里也出现过，侥幸被收进来；
    #      · **只在这里出现过的字就永远是方框** —— 而且 --check 也查不出来，
    #        因为 check 的"源码字符集"同样漏扫了它（两个 bug 互相掩盖）。
    #    真机实例：控制中心「请耐心等待」的「耐」字就是这么漏的（屏上是方框）。
    #    顺带把历史上那 23 个一直缺的字（佐侥俩兑剥劈吓吭唐巧幸挺熟甲砖窜筛肌胶荒诀迷麻）
    #    一起收进来了 —— 它们本就来自这一层的文案。
    os.path.join(ROOT, "components", "sdgoods_launcher"),
]
# 生成物所在目录不参与扫描：否则「上一轮生成的字」会被当成源码字，字符集只增不减
SKIP_DIRS = {os.path.normpath(FONTS_DIR), os.path.normpath(os.path.join(ROOT, "main", "patches"))}
SKIP_PREFIX = ("cn_font_", "si_yuan_black_icon_")

DEFAULT_FONT = os.path.join(ROOT, "tools", "fonts", "NotoSansSC-Regular.ttf")

# 半角 + 全角常用标点
# ⚠️ 这里**故意不加**全角 `？！`（U+FF1F / U+FF01），与 tools/gen_fontpack.py 的 PUNCT
#    **有意不一致** —— 理由是这套产物（cn_font_* / si_yuan_black_icon_*，落在
#    components/sdgoods_board/fonts/）**已不再编译进 launcher 固件**（见
#    components/sdgoods_board/CMakeLists.txt 与 sdgoods_fonts.h 顶部说明）；
#    中文上屏现在全走**运行时语言包**（tools/gen_fontpack.py）。
#    改了这里的 PUNCT 就得连带重产那四个 .c，而它们谁都不影响 ⇒ 只会让 `--check`
#    多报两个字（25 vs 23）、白造一堆 diff。**需要全角 `？！` 的是语言包那条链路**，
#    那边的 PUNCT 已于 2026-10-13 补齐（背景见 tools/verify_shots/
#    queued_del_font_2026-10-13/README.md）。
PUNCT = """!"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~”“、。，：；（）—‘’·…《》【】"""

# 不在 CJK 区段也不在 ASCII 范围的特殊符号，必须显式列出：
#   ♥ U+2665 实心爱心（生命道具 / 生命心 HUD）
#   ♡ U+2661 空心爱心（备用）
EXTRA_SYMBOLS = "\u2665\u2661"

# 「已知例外」白名单：**源字体确实没有字形**、且**永远不上屏**的字符。
# 🔴 2026-11-02 起**默认应为空**，因为「日志/断言/告警里的字符串」已由
#    OFFSCREEN_MARKERS 按**出现位置**结构性排除了（见下方 _is_offscreen）——
#    原来唯一一条 `↳`(U+21B3) 只出现在 ESP_LOGE 里，现在自动被跳过。
# 🔴 这个表是「已知例外」，不是垃圾桶：
#    · 屏上文案用的字符**一律不准**进这里 —— 那样等于用白名单掩盖方框。
#    · 真要用：必须同时写清「它出现在哪个文件的哪一句、为什么永不上屏」。
#    · 条数超过 20 就说明白名单在掩盖真问题（字体选错了 / 文案里有异体符号），
#      脚本会直接报错而不是继续放过。
NOT_ON_SCREEN = ""
NOT_ON_SCREEN_MAX = 20

# 主字体（si_yuan_black_icon_*）的精选字符集：日常界面/菜单/游戏 HUD 用到的那批字。
# 之所以单独收一份而不是直接用全量集，是为了让「主字体」体积最小、加载路径最短。
# 新增界面文案如果落在精选集之外，会由 fallback（cn_font_*）兜住，功能上不会缺字；
# 想让新字用主字体渲染，把它补进这个字符串即可。
ICON_SYMBOLS = (
    "个中亮仓他仪件信关其击分动压取可后回声备始子小屏幕应度开录徽息戏扫按描播放数"
    "机束池游点牙玩用电硬空章结自蓝螺设读谷近返重键间陀附音鸟"
    "，：！？、（）—…·“”‘’。《》【】；第过升级最"
)

LICENSE_HEADER = """/*
 * ⚠️ 本文件由 tools/gen_fonts.py 自动生成 —— 请勿手工编辑。
 *    要改字号或字符集：改脚本，或直接跑  python3 tools/gen_fonts.py
 *
 * 字形来源：Noto Sans SC
 *   Copyright (c) 2014-2021 Adobe (http://www.adobe.com/),
 *   with Reserved Font Name 'Source'.
 *
 * 许可：SIL Open Font License, Version 1.1
 *       https://scripts.sil.org/OFL
 *   该许可允许把字形嵌入到软件中并随软件再分发（含商用）；
 *   要求保留本版权与许可声明，且不得单独以字体文件形式售卖。
 *
{extra} */

"""


def _strip_comments(src):
    """去掉注释（块 + 行），**保留字符串字面量原样**。

    为什么要区分这两者：
      · 源码注释里有大量 emoji（⚠️ 🔴 ✅ →）和示意图，它们永远不该进字库 ——
        全量收进去会让每次烘焙都报「缺字」（Noto Sans SC 没有 emoji）。
      · 字符串字面量里的符号（例如 "°C"）**必须**进字库，否则屏上是方框。
    历史上只按 [\\u4e00-\\u9fff] 扫，于是 ° ✓ ℃ 这类符号一个都收不到；
    而 check 又只比「传进去的清单」，两个 bug 互相掩盖了整整一轮。
    """
    out = []
    i, n = 0, len(src)
    state = None          # None / 'line' / 'block' / 'str'
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if state is None:
            if two == '/*':
                state = 'block'; i += 2; continue
            if two == '//':
                state = 'line'; i += 2; continue
            if c == '"':
                state = 'str'; out.append(c); i += 1; continue
            out.append(c); i += 1; continue
        if state == 'line':
            if c == '\n':
                state = None; out.append(c)
            i += 1; continue
        if state == 'block':
            if two == '*/':
                state = None; i += 2; continue
            i += 1; continue
        # 字符串 / 字符字面量内部
        if c == '\\':
            out.append(src[i:i + 2]); i += 2; continue
        if state == 'str' and c == '"':
            state = None; out.append(c); i += 1; continue
        if c == '\n':            # 未闭合的引号：保险起见退出，别把后面的注释全吞了
            state = None; out.append(c); i += 1; continue
        out.append(c); i += 1
    return ''.join(out)


# --------------------------------------------------------------------------- 非上屏字符串
# 「日志 / 断言 / 编译告警」里的字符串**永远不上屏**，不该进字库。
# 🔴 必须按**出现位置**排除，不能靠「把缺的字塞进 NO_ON_SCREEN 白名单」：
#    · 白名单是按**字符**放行的 ⇒ 今天为一句 ESP_LOGE 放过「」，明天谁把它写进
#      界面文案就静默变方框，而 --check 照样绿（正是「白名单掩盖问题」那条红线）。
#    · 按调用点排除则相反：同一个字符出现在日志里被跳过、出现在 set_text 里照样收。
#    实测来源（2026-11-02）：`「」` 在 i18n.c 的 ESP_LOGI、`×≤` 在 slot_cover.c 的
#    _Static_assert、`§` 在 app_sdk.c 的 ESP_LOGE/#warning、`「」` 在
#    main/gen_build_version.cmake 被拼成 C **注释**的字符串里 —— 一个都不上屏。
OFFSCREEN_MARKERS = (
    "ESP_LOG", "ESP_EARLY_LOG", "ESP_DRIVER_LOG",                        # 串口日志
    "ESP_STATIC_ASSERT", "_Static_assert", "static_assert", "assert(",    # 断言消息
    "#warning", "#error",                                                # 编译告警
)
_STMT_BOUNDARY = ";{}"          # 语句边界：往回找上下文时到这里就停
_WINDOW_MAX = 400               # 往回最多看这么多字符（跨行日志宏够用）
_LITERAL_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def _is_offscreen(stripped, start):
    """`start` 处的字符串字面量是否处在「不上屏」上下文里。

    做法：从字面量位置往回扫到最近的语句边界（`;` `{` `}`），看这段里有没有
    日志/断言/告警宏名。跨行日志宏（`ESP_LOGI(TAG,\\n "…")`）也能命中，因为
    边界只在语句处停。
    """
    j = start
    while j > 0 and stripped[j - 1] not in _STMT_BOUNDARY and (start - j) < _WINDOW_MAX:
        j -= 1
    head = stripped[j:start]
    return any(m in head for m in OFFSCREEN_MARKERS)


def _chars_of(src):
    """从一段 C 源码里收集**会上屏**的非 ASCII 字符（剥注释 + 跳过非上屏字面量）。"""
    stripped = _strip_comments(src)
    chars = set()
    for m in _LITERAL_RE.finditer(stripped):
        if _is_offscreen(stripped, m.start()):
            continue
        for ch in m.group(1):
            if ord(ch) > 0x7E:      # 非 ASCII：汉字、全角标点、° ✓ ℃ …
                chars.add(ch)
    return chars


def _chars_of_cmake(src):
    """`.cmake` 特殊：它的引号内容会被拼成**一份 C 源码**写进生成文件。

    所以先把字面量还原成文本、再按 C 规则剥注释 —— 否则像
    `"/* … 固件的「身份证」 … */"` 这种「将来是注释」的文案会被当成上屏字符。
    """
    parts = []
    for m in _LITERAL_RE.finditer(src):
        t = m.group(1)
        t = t.replace('\\n', '\n').replace('\\"', '"').replace('\\t', '\t')
        parts.append(_strip_comments(t))
    return _chars_of(''.join(parts))


def collect_chars():
    """扫描源码，收集**会上屏的字符串里出现的所有非 ASCII 字符**。

    不再只认汉字：`°C` 的 `°`(U+00B0)、`✓`、`℃` 这些都不在 \\u4e00-\\u9fff 里，
    旧实现直接漏掉 ⇒ 烘不进字库 ⇒ 屏上方框，而校验还说"无缺字"。
    """
    chars = set()
    for base in SCAN_DIRS:
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if os.path.normpath(os.path.join(dirpath, d)) not in SKIP_DIRS]
            if os.path.normpath(dirpath) in SKIP_DIRS:
                continue
            for name in files:
                if not name.endswith((".c", ".h", ".inc", ".cmake")):
                    continue
                if name.startswith(SKIP_PREFIX):
                    continue
                with open(os.path.join(dirpath, name), encoding="utf-8", errors="ignore") as f:
                    src = f.read()
                chars |= _chars_of_cmake(src) if name.endswith(".cmake") else _chars_of(src)
    chars.update(PUNCT)
    chars.update(EXTRA_SYMBOLS)
    return chars


def run_conv(bin_path, font, size, bpp, symbols, name, out):
    cmd = [
        bin_path, "--font", font,
        "--size", str(size), "--bpp", str(bpp),
        "--format", "lvgl", "--no-compress", "--no-prefilter",
        "--symbols", symbols,
        "--range", "0x20-0x7E",
        "--lv-font-name", name,
        "--lv-include", "lvgl.h",
        "-o", out,
    ]
    subprocess.run(cmd, check=True)


# --------------------------------------------------------------------------- 绝对路径归一化
# 为什么需要：lv_font_conv 会把**完整命令行**回写进产物头部注释（见上面「可移植性」）。
# 不处理的话，维护者本机家目录下的私有工作区（…/WorkBuddy/<日期>/…）就会随字体源文件进仓库 ——
# 2026-10-04 开源就绪度体检定的 P0-1。
# 🔴 模式写成**正则**而不是裸字面量：否则本文件自己就会被「全仓搜本机绝对路径」那条
#    验收判据命中，污染结论（自指污染）。
_LEAK_RE = re.compile(
    r"/(?:Users|home|root)/"                                  # POSIX 私有目录
    r"|[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/]"    # Windows 私有目录
)


def norm_path_arg(p):
    """把一个命令行路径参数改成可移植写法（只影响注释，不碰任何字形数据）。"""
    if ROOT + os.sep in p:                                   # 仓库内 → 相对仓库根
        return p.replace(ROOT + os.sep, "")
    if p.startswith("/") or re.match(r"[A-Za-z]:[\\/]", p):   # 仓库外 → 只留文件名
        return "<abs>/" + os.path.basename(p.replace("\\", "/"))
    return p


def sanitize_opts_comment(src):
    """归一化产物里 `Opts:` 注释行的 --font / -o 路径参数。"""
    lines = []
    for line in src.splitlines(keepends=True):
        if "Opts:" in line:
            toks = line.split(" ")
            for i, t in enumerate(toks[:-1]):
                if t in ("--font", "-o"):
                    toks[i + 1] = norm_path_arg(toks[i + 1])
            line = " ".join(toks)
        lines.append(line)
    return "".join(lines)


def assert_no_host_paths(out, src):
    """守卫：产物里绝不允许出现本机私有路径。

    归一化依赖 lv_font_conv 头部注释的**格式**；哪天上游改了格式或换了工具，
    就会静默把路径写回去。这里让它**当场炸**，而不是等第三方在仓库里看见。
    """
    hit = _LEAK_RE.search(src)
    if hit:
        raise RuntimeError(
            f"{out} 里仍有本机路径（命中 {hit.group(0)!r}）—— 请检查 lv_font_conv 的头部格式，"
            f"必要时同步更新 sanitize_opts_comment()"
        )
    if ROOT + os.sep in src:
        raise RuntimeError(f"{out} 里仍有仓库绝对路径（{ROOT}）—— 归一化没生效")


def postprocess(out, size, name, fallback_name=None):
    """加 OFL 许可头；主字体额外注入 .fallback 指向全量字体；归一化绝对路径。"""
    src = open(out, encoding="utf-8").read()
    src = sanitize_opts_comment(src)

    extra = ""
    if fallback_name:
        extra = "\n".join([
            " * fallback：本字体只含 UI 精选子集，缺字时由 LVGL 自动回退到 " + fallback_name + "。",
            " *   这里用「编译期 LV_FONT_DECLARE + .fallback」而不是运行时赋值 ——",
            " *   运行时写 const 字体结构体会触发 ESP32 flash Cache 错误（非法写 flash）。",
        ])
    header = LICENSE_HEADER.format(extra=extra)

    if fallback_name:
        decl = (f"LV_FONT_DECLARE({fallback_name});   /* 全量中文子集，作为本字体的 fallback */\n")
        # 插到 #ifndef <NAME> 之前（即 include 段之后）
        upper = name.upper()
        marker = f"#ifndef {upper}"
        if marker in src:
            src = src.replace(marker, decl + "\n" + marker, 1)
        else:
            raise RuntimeError(f"在 {out} 里找不到 {marker}，无法注入 fallback 声明")
        if ".fallback = NULL," not in src:
            raise RuntimeError(f"在 {out} 里找不到 '.fallback = NULL,'，无法注入 fallback")
        src = src.replace(".fallback = NULL,", f".fallback = &{fallback_name},", 1)

    assert_no_host_paths(out, header + src)
    open(out, "w", encoding="utf-8").write(header + src)


def font_charset(path):
    """解析生成的 .c 里**真实存在**的字形编码集合。

    🔴🔴 为什么不能用头部 Opts 里的 --symbols：那是**我们要求过什么**，
        不是**字体里真有什么**。lv_font_conv 对源字体没有的字形是**静默跳过**的
        （不报错、不警告），于是 check 拿「要求的」比「要求的」，永远相等 ——
        恒真的判据就等于没判。实测踩过：`°`(U+00B0) 不在 Noto Sans SC 里，
        屏上显示方框，而 check 报「无缺字」。

    真实字形在 cmaps 里：
      · .unicode_list = NULL            → 连续区间 range_start .. +range_length-1
      · .unicode_list = unicode_list_N  → 稀疏表，编码 = range_start + 表里每个偏移
    """
    src = open(path, encoding="utf-8").read()

    lists = {}
    for m in re.finditer(r"static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        lists[m.group(1)] = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", m.group(2))]

    chars = set()
    pat = re.compile(
        r"\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,(.*?)\.type\s*=\s*LV_FONT_FMT_TXT_CMAP_(\w+)",
        re.S,
    )
    for m in pat.finditer(src):
        start, length, body, _kind = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", body)
        if ul and ul.group(1) != "NULL" and ul.group(1) in lists:
            for off in lists[ul.group(1)]:
                chars.add(start + off)
        elif length <= 4096:
            chars.update(range(start, start + length))
        else:
            # 没有 unicode_list 却声称超大区间 = 解析没认出来，宁可报错也别静默放过
            raise SystemExit(f"✗ {path}: cmaps 里有个 {length} 长的区间却没有 unicode_list，解析不了")
    return chars


def check(path, required, label):
    """逐字校验**字体里真的有这些字形**（不是校验我们传了什么）。"""
    if not os.path.isfile(path):
        print(f"  ✗ {label}: 文件不存在 {path}")
        return False
    try:
        have = font_charset(path)
    except SystemExit as e:
        print(f"  ✗ {label}: {e}")
        return False
    if not have:
        print(f"  ⚠️ {label}: 没解析出任何字形，跳过校验（判据无效，请检查 lv_font_conv 版本）")
        return True
    missing_all = sorted({c for c in required if ord(c) not in have}, key=ord)
    if len(NOT_ON_SCREEN) > NOT_ON_SCREEN_MAX:
        print(f"  ✗ 不上屏白名单有 {len(NOT_ON_SCREEN)} 个字符，超过上限 {NOT_ON_SCREEN_MAX}"
              f" —— 白名单在掩盖问题，请先查清这些字为什么缺")
        return False
    ignored = [c for c in missing_all if c in NOT_ON_SCREEN]
    missing = [c for c in missing_all if c not in NOT_ON_SCREEN]
    if ignored:
        print(f"  · {label}: 忽略 {len(ignored)} 个不上屏字符：{''.join(ignored)}"
              f"（只出现在串口日志里，屏上不显示）")
    if missing:
        print(f"  ✗ {label}: 缺 {len(missing)} 个字：{''.join(missing[:60])}")
        # 把编码也打出来：方框在屏上是同一个形状，没编码根本查不出缺的是哪个符号
        print(f"       编码：{' '.join('U+%04X' % ord(c) for c in missing[:12])}")
        return False
    print(f"  ✓ {label}: {len(have)} 个字形，无缺字")
    return True


def check_pair(size, charset):
    """校验一对字体。
    注意两套字体的预期字符集**不同**：
      · cn_font_*          全量集（它的职责就是兜底，必须一个不缺）
      · si_yuan_black_*    精选集（故意只收 UI 常用字，缺的字交给 fallback）
    """
    cn = os.path.join(FONTS_DIR, f"cn_font_{size}.c")
    sy = os.path.join(FONTS_DIR, f"si_yuan_black_icon_{size}.c")
    ok = check(cn, charset, f"cn_font_{size}")
    ok &= check(sy, set(ICON_SYMBOLS), f"si_yuan_black_icon_{size}")
    # 主字体必须真的挂上了 fallback，否则精选集外的字会显示成方框
    if os.path.isfile(sy):
        body = open(sy, encoding="utf-8").read()
        if f".fallback = &cn_font_{size}," not in body:
            print(f"  ✗ si_yuan_black_icon_{size}: 没有挂上 .fallback = &cn_font_{size}，"
                  f"精选集外的字会变方框")
            ok = False
    return ok


def _selftest():
    """正反成对自检：同一个字符，上屏必须收、日志/断言必须跳过。

    为什么要成对：只测「日志里的被跳过」⇒ 把所有字面量都跳过也照样绿（判据自己失效）；
    只测「上屏的收进来」⇒ 全量收也绿（等于没过滤）。两条一起才钉得住边界。
    合成源码而不是改真文件：跑一次不影响仓库，任何机器上结果一致。
    """
    cases = [
        # (说明, 源码, 必须收的字, 必须不收的字)
        ("set_text 上屏", 'lv_label_set_text(l, "温度 25℃「甲」");', "℃「", None),
        ("SDG_T 双语上屏", 'lbl = SDG_T("必须重启", "restart");', "必须", None),
        ("ESP_LOGI 单行", 'void f(void){ ESP_LOGI(TAG, "语言 = %s「乙」", x); }', None, "语言「乙"),
        ("ESP_LOGI 跨行（宏名在上一行）",
         'void f(void){\n    ESP_LOGI(TAG,\n             "封面必须塞得进「丙」");\n}', None, "封面必须「丙"),
        ("ESP_LOGE 续行拼接",
         'void f(void){ ESP_LOGE(TAG, "第一句"\n                          "§2.1「丁」"); }', None, "§「丁"),
        ("_Static_assert 消息",
         '_Static_assert(sizeof(a) == 8, "宽度 = 132×132 ≤ 上限");', None, "宽度×≤"),
        ("#warning", '#warning "见 docs/BUILD.md §3「戊」"', None, "§「戊"),
        # 🔴 关键反例：日志之后紧跟上屏字面量（同一函数内），窗口不能吃过界。
        #    两个字符串用**不同**的标点，才能证明是「按位置跳过」而不是「按字符跳过」。
        ("日志之后的上屏文案不被连带跳过",
         'void f(void){\n    ESP_LOGI(TAG, "日志〔己〕");\n    lv_label_set_text(l, "重试「庚」");\n}',
         "重试「", "日志〔己〕"),
        ("注释里的字仍然不收", '/* 〔辛〕 × ≤ */\nlv_label_set_text(l, "好");', "好", "〔辛〕×≤"),
    ]
    fails = 0
    for what, src, must_in, must_out in cases:
        got = _chars_of(src)
        for ch in (must_in or ""):
            if ch not in got:
                print(f"  ✗ {what}: 应收集 {ch!r}(U+{ord(ch):04X}) 却没收到 —— 会漏烘 ⇒ 屏上方框")
                fails += 1
        for ch in (must_out or ""):
            if ch in got:
                print(f"  ✗ {what}: 不该收 {ch!r}(U+{ord(ch):04X}) 却收进来了 —— 日志字进字库，"
                      f"白占体积且掩盖真缺字")
                fails += 1
    # cmake：引号内容会拼成一份 C 源码，里面的 /* */ 是注释，不该算上屏
    cm = _chars_of_cmake('"/* ---- 品牌：固件的「身份证」 ---- */\\n"\n'
                         '"#define SDGOODS_PROGRAM      \\"谷仓共创计划\\"\\n"\n')
    for ch in "谷仓共创计划":
        if ch not in cm:
            print(f"  ✗ cmake 上屏宏: 漏收 {ch!r} —— 关于页会缺字")
            fails += 1
    if "「" in cm:
        print("  ✗ cmake: 把生成文件里的 C 注释当成上屏文案了（「 不该被收）")
        fails += 1

    total = (sum(len(x[2] or "") + len(x[3] or "") for x in cases)
             + len("谷仓共创计划") + 1)      # +1 = cmake 注释那条
    if fails:
        print(f"\n自检失败 {fails} 处 /{total} 条断言")
        return 1
    print(f"自检通过：{total} 条断言（上屏必收 / 日志·断言·告警必跳 / cmake 注释不当文案）")
    return 0


def main():
    ap = argparse.ArgumentParser(description="重新生成 LVGL 中文字体子集（OFL 授权）")
    ap.add_argument("--font", default=os.environ.get("FONT_SRC", DEFAULT_FONT),
                    help="源字体路径（默认 tools/fonts/NotoSansSC-Regular.ttf）")
    ap.add_argument("--bin", default=os.environ.get("LV_FONT_CONV") or shutil.which("lv_font_conv") or "lv_font_conv",
                    help="lv_font_conv 可执行文件路径")
    ap.add_argument("--bpp", default="4", help="每像素位深（默认 4：兼顾体积与抗锯齿质量）")
    ap.add_argument("--sizes", default="14,16", help="字号，逗号分隔")
    ap.add_argument("--check", action="store_true", help="只校验现有字体是否缺字，不重新生成")
    ap.add_argument("--selftest", action="store_true",
                    help="自检 char 收集的正反断言（上屏字面量必须收、日志/断言里的必须跳过）")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(_selftest())

    charset = collect_chars()
    symbols_all = "".join(sorted(charset, key=ord))
    sizes = [s.strip() for s in args.sizes.split(",") if s.strip()]
    print(f"源码字符集: {len(symbols_all)} 个字符（ASCII 由 --range 0x20-0x7E 覆盖）")

    if args.check:
        ok = True
        for size in sizes:
            ok &= check_pair(size, charset)
        sys.exit(0 if ok else 1)

    if not os.path.isfile(args.font):
        sys.exit(f"源字体不存在: {args.font}\n先跑一次：python3 tools/fetch_fonts.py")
    if not (os.path.isfile(args.bin) or shutil.which(args.bin)):
        sys.exit(f"找不到 lv_font_conv: {args.bin}\n请先 npm i lv_font_conv，再用 --bin 指定路径")

    os.makedirs(FONTS_DIR, exist_ok=True)
    for size in sizes:
        # 1) 全量子集（fallback）
        out_cn = os.path.join(FONTS_DIR, f"cn_font_{size}.c")
        print(f"生成 cn_font_{size} -> {out_cn}")
        run_conv(args.bin, args.font, size, args.bpp, symbols_all, f"cn_font_{size}", out_cn)
        postprocess(out_cn, size, f"cn_font_{size}")

        # 2) UI 精选子集（主字体，fallback 指向上面的全量字体）
        out_sy = os.path.join(FONTS_DIR, f"si_yuan_black_icon_{size}.c")
        print(f"生成 si_yuan_black_icon_{size} -> {out_sy}")
        run_conv(args.bin, args.font, size, args.bpp, ICON_SYMBOLS, f"si_yuan_black_icon_{size}", out_sy)
        postprocess(out_sy, size, f"si_yuan_black_icon_{size}", fallback_name=f"cn_font_{size}")

    print("校验生成结果...")
    ok = True
    for size in sizes:
        ok &= check_pair(size, charset)
    if not ok:
        sys.exit("✗ 生成结果有问题，请检查上面的报告")
    print("全部完成。记得重新编译（idf.py build）后用 tools/screenshot_recv.py 截屏确认排版。")


if __name__ == "__main__":
    main()
