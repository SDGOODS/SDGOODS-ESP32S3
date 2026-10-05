#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""launcher 版本号自增 —— 进位规则：末位只到 99，满 99 进上位。

用户口径（2026-10-05 明确）：
  · launcher **每次编译版本号 +1**（末位 +1）；
  · 末位到 99 之后再 +1 ⇒ **中位 +1、末位归 0**：`2.0.99` → `2.1.0`。

为什么要脚本：99 那一步手算极易写错（`2.0.100` / `2.1.99` 都是典型错法）。
把规则固化成代码并自带用例，比"记得住"可靠。

真相源：**工程根 `version.txt`**。它被
`components/sdgoods_launcher/tools/gen_app_info.py` **透传**为 `SDGOODS_APP_VERSION`，
CMake 的 `project(... VERSION)` 也读同一文件 ⇒ 二进制里的 `esp_app_desc.version`
与关于页显示的版本**同源、恒等**（历史上曾经差 1，已修）。所以**只改这一处**。

用法：
  tools/bump_version.py              # 2.1.1 → 2.1.2，写回 version.txt
  tools/bump_version.py --dry-run    # 只打印结果，不动文件
  tools/bump_version.py --set 2.2.0  # 直接指定（不走进位，用于修手误）
  tools/bump_version.py --quiet      # 只输出新版本号（给别的脚本用）
  tools/bump_version.py --self-test  # 跑一遍进位用例，验证规则

⚠️ 改完 version.txt **必须重新编译**才会进镜像（这一步不做就白改）。
"""

import argparse
import os
import re
import sys

# 末位（patch）到 99 就进位 —— 用户口径
MAX_PATCH = 99
# 中位（minor）同理。用户只点名了末位进位；中位这一层是同一条规则的推广
# （否则 2.99.xx 没有出路）。实际一年也走不到，写出来只为不留死路。
MAX_MINOR = 99

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION_FILE = os.path.join(ROOT, "version.txt")


def parse(text):
    """'2.1.1' -> (2, 1, 1)。格式不对直接报错，别静默猜。"""
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", text.strip())
    if not m:
        raise ValueError("version.txt 必须是 A.B.C 三个数字，实际读到 '%s'" % text.strip())
    return tuple(int(g) for g in m.groups())


def bump(ver):
    """末位 +1；过 99 则进位（末位归 0、中位 +1），中位过 99 同理再往上进。"""
    major, minor, patch = ver
    patch += 1
    if patch > MAX_PATCH:
        patch = 0
        minor += 1
        if minor > MAX_MINOR:
            minor = 0
            major += 1
    return (major, minor, patch)


def fmt(ver):
    return "%d.%d.%d" % ver


SELF_TEST_CASES = [
    # (输入, 期望输出, 说明)
    ((2, 1, 1), (2, 1, 2), "普通 +1"),
    ((2, 0, 98), (2, 0, 99), "末位刚好到 99，不进位"),
    ((2, 0, 99), (2, 1, 0), "★ 用户给的例子：满 99 进中位、末位归 0"),
    ((2, 1, 99), (2, 2, 0), "中位不是 0 时同样只进中位"),
    ((2, 99, 99), (3, 0, 0), "中位也满 99 ⇒ 再往上进（规则的推广）"),
    ((1, 0, 0), (1, 0, 1), "首版起步"),
]


def self_test():
    bad = 0
    for src, want, note in SELF_TEST_CASES:
        got = bump(src)
        ok = got == want
        bad += 0 if ok else 1
        print("  %s %-10s -> %-10s 期望 %-10s  %s"
              % ("✅" if ok else "❌", fmt(src), fmt(got), fmt(want), note))
    print("\n%d/%d 用例通过" % (len(SELF_TEST_CASES) - bad, len(SELF_TEST_CASES)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description="launcher 版本号自增（末位满 99 进位）")
    ap.add_argument("--dry-run", action="store_true", help="只打印，不写回")
    ap.add_argument("--set", metavar="A.B.C", help="直接指定新版本号（不走进位）")
    ap.add_argument("--quiet", action="store_true", help="只输出新版本号")
    ap.add_argument("--self-test", action="store_true", help="跑进位用例")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not os.path.isfile(VERSION_FILE):
        print("❌ 找不到 %s" % VERSION_FILE, file=sys.stderr)
        return 1

    with open(VERSION_FILE, "r", encoding="utf-8") as f:
        raw = f.read()

    cur = parse(raw)
    if args.set:
        new = parse(args.set)
    else:
        new = bump(cur)

    if new == cur:
        print("⚠️ 新版本号与当前相同（%s），没有变化" % fmt(cur), file=sys.stderr)

    if args.dry_run:
        if args.quiet:
            print(fmt(new))
        else:
            print("版本号 %s -> %s（--dry-run，未写回）" % (fmt(cur), fmt(new)))
        return 0

    with open(VERSION_FILE, "w", encoding="utf-8") as f:
        f.write(fmt(new) + "\n")

    if args.quiet:
        print(fmt(new))
    else:
        print("版本号 %s -> %s（已写回 %s）" % (fmt(cur), fmt(new), VERSION_FILE))
        print("⚠️ 别忘了重新编译 —— 不编译的话镜像里还是 %s。" % fmt(cur))
    return 0


if __name__ == "__main__":
    sys.exit(main())
