#!/usr/bin/env python3
"""重新生成 LVGL 中文子集字体。

背景
----
固件体积有限，不可能把整套中文字库塞进 flash，所以项目里用的是「子集字体」：
扫描 main/ 下所有 .c/.h 用到的字符，只把这些字形烘进 .c 字体文件。
好处是字体只占几百 KB；代价是**新增 UI 文案后必须重跑本脚本**，
否则屏幕上会出现方框（tofu）。

用法
----
    python3 tools/gen_fonts.py                       # 用默认字体
    python3 tools/gen_fonts.py --font ~/fonts/NotoSansSC-Regular.otf
    python3 tools/gen_fonts.py --bin /path/to/lv_font_conv

依赖
----
1. Node.js 与 lv_font_conv（官方字体转换工具）：

       npm i lv_font_conv          # 装到任意目录
       # 然后指定： --bin ./node_modules/.bin/lv_font_conv

2. 一个包含所需汉字的 TTF/OTF 源字体。

⚠️ 关于源字体的授权：请使用**授权明确允许再分发**的字体，推荐 SIL OFL 1.1 的
   思源黑体 / Noto Sans SC。脚本默认路径指向 macOS 自带的 Arial Unicode，
   仅为本机复现旧版字体而保留；若要对外发布固件，建议换成 OFL 字体后重新生成。

脚本会做两件事：
  1. 扫描源码字符集 → 调 lv_font_conv 生成 main/cn_font_14.c 与 cn_font_16.c；
  2. 读回生成文件头部的 --symbols 清单，逐字校验，缺字直接报错退出。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "main")

# 常用标点（半角 + 全角）
PUNCT = """!"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~”“、。，：；（）—‘’·…"""

# 不在 CJK 区段也不在 ASCII 范围的特殊符号，必须显式列出：
#   ♥ U+2665 实心爱心（生命道具 / 生命心 HUD）
#   ♡ U+2661 空心爱心（备用）
EXTRA_SYMBOLS = "\u2665\u2661"


def collect_chars():
    """扫描 main 下所有 .c/.h，收集全部 CJK 字符。"""
    chars = set()
    cjk = re.compile(r"[\u4e00-\u9fff]+")
    for name in sorted(os.listdir(MAIN)):
        if not name.endswith((".c", ".h", ".inc", ".cmake")):
            continue
        path = os.path.join(MAIN, name)
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for m in cjk.findall(f.read()):
                chars.update(m)
    chars.update(PUNCT)
    chars.update(EXTRA_SYMBOLS)
    return chars


def main():
    ap = argparse.ArgumentParser(description="重新生成 LVGL 中文子集字体")
    ap.add_argument("--font", default=os.environ.get(
        "FONT_SRC", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"),
        help="源字体路径（推荐用 OFL 授权的思源黑体 / Noto Sans SC）")
    ap.add_argument("--bin", default=os.environ.get("LV_FONT_CONV")
                    or shutil.which("lv_font_conv") or "lv_font_conv",
                    help="lv_font_conv 可执行文件路径")
    ap.add_argument("--bpp", default="4", help="每像素位深（默认 4，兼顾体积与质量）")
    ap.add_argument("--sizes", default="14,16", help="要生成的字号，逗号分隔")
    args = ap.parse_args()

    if not os.path.isfile(args.font):
        sys.exit(f"源字体不存在: {args.font}")
    if not (os.path.isfile(args.bin) or shutil.which(args.bin)):
        sys.exit(f"找不到 lv_font_conv: {args.bin}\n"
                 f"请先 npm i lv_font_conv，再用 --bin 指定路径")

    charset = collect_chars()
    symbols = "".join(sorted(charset, key=ord))
    print(f"源码字符集: {len(symbols)} 个字符（ASCII 由 --range 覆盖）")

    sizes = [s.strip() for s in args.sizes.split(",") if s.strip()]
    for size in sizes:
        out = os.path.join(MAIN, f"cn_font_{size}.c")
        cmd = [
            args.bin,
            "--font", args.font,
            "--size", size,
            "--bpp", args.bpp,
            "--format", "lvgl",
            "--no-compress",
            "--no-prefilter",
            "--symbols", symbols,
            "--range", "0x20-0x7E",
            "--lv-font-name", f"cn_font_{size}",
            "--lv-include", "lvgl.h",
            "-o", out,
        ]
        print(f"生成 cn_font_{size} -> {out}")
        subprocess.run(cmd, check=True)

    # 校验：读回生成文件头部注释里的 --symbols，确保没有漏字（否则屏上出方框）
    print("校验生成结果...")
    ok = True
    for size in sizes:
        out = os.path.join(MAIN, f"cn_font_{size}.c")
        with open(out, "r", encoding="utf-8") as f:
            head = "".join(f.readline() for _ in range(6))
        m = re.search(r"--symbols\s+(.*?)\s+--range", head, re.S)
        if not m:
            print(f"  错误: 无法从 {out} 解析 --symbols", file=sys.stderr)
            ok = False
            continue
        generated = set(m.group(1))
        missing = sorted(charset - generated, key=ord)
        if missing:
            print(f"  错误: cn_font_{size} 缺字 {''.join(missing)}（{len(missing)} 个）",
                  file=sys.stderr)
            ok = False
        else:
            print(f"  cn_font_{size}: OK（{len(generated)} 个符号）")

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
