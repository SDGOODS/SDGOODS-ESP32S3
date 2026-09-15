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
]
# 生成物所在目录不参与扫描：否则「上一轮生成的字」会被当成源码字，字符集只增不减
SKIP_DIRS = {os.path.normpath(FONTS_DIR), os.path.normpath(os.path.join(ROOT, "main", "patches"))}
SKIP_PREFIX = ("cn_font_", "si_yuan_black_icon_")

DEFAULT_FONT = os.path.join(ROOT, "tools", "fonts", "NotoSansSC-Regular.ttf")

# 半角 + 全角常用标点
PUNCT = """!"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~”“、。，：；（）—‘’·…《》【】"""

# 不在 CJK 区段也不在 ASCII 范围的特殊符号，必须显式列出：
#   ♥ U+2665 实心爱心（生命道具 / 生命心 HUD）
#   ♡ U+2661 空心爱心（备用）
EXTRA_SYMBOLS = "\u2665\u2661"

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


def collect_chars():
    """扫描源码，收集所有 CJK 字符 + 标点 + 特殊符号。"""
    chars = set()
    cjk = re.compile(r"[\u4e00-\u9fff]+")
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
                    for m in cjk.findall(f.read()):
                        chars.update(m)
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


def postprocess(out, size, name, fallback_name=None):
    """加 OFL 许可头；主字体额外注入 .fallback 指向全量字体。"""
    src = open(out, encoding="utf-8").read()

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

    open(out, "w", encoding="utf-8").write(header + src)


def check(path, required, label):
    """读回生成的 .c 头部 --symbols 清单，逐字校验是否缺字。"""
    if not os.path.isfile(path):
        print(f"  ✗ {label}: 文件不存在 {path}")
        return False
    head = "".join(open(path, encoding="utf-8").readlines()[:24])
    m = re.search(r"--symbols\s+(.*?)\s+--range", head, re.S)
    if not m:
        print(f"  ⚠️ {label}: 无法解析 --symbols，跳过校验")
        return True
    generated = set(m.group(1))
    missing = sorted(required - generated, key=ord)
    if missing:
        print(f"  ✗ {label}: 缺 {len(missing)} 个字：{''.join(missing[:60])}")
        return False
    print(f"  ✓ {label}: {len(generated)} 个符号，无缺字")
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


def main():
    ap = argparse.ArgumentParser(description="重新生成 LVGL 中文字体子集（OFL 授权）")
    ap.add_argument("--font", default=os.environ.get("FONT_SRC", DEFAULT_FONT),
                    help="源字体路径（默认 tools/fonts/NotoSansSC-Regular.ttf）")
    ap.add_argument("--bin", default=os.environ.get("LV_FONT_CONV") or shutil.which("lv_font_conv") or "lv_font_conv",
                    help="lv_font_conv 可执行文件路径")
    ap.add_argument("--bpp", default="4", help="每像素位深（默认 4：兼顾体积与抗锯齿质量）")
    ap.add_argument("--sizes", default="14,16", help="字号，逗号分隔")
    ap.add_argument("--check", action="store_true", help="只校验现有字体是否缺字，不重新生成")
    args = ap.parse_args()

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
