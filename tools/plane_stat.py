#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""飞机游戏「道具」统计工具（配合 build_fix44 的新火力/生命道具改动）。

用途：
  1) 边玩边抓两边设备的串口日志，实时汇总道具掉落种类分布、拾取事件、火力档位。
  2) 事后分析已保存的日志文件。

用法：
  python3 plane_stat.py                       # 监听 A/B 两台设备，默认 90 秒
  python3 plane_stat.py -d 60                 # 监听 60 秒
  python3 plane_stat.py --log plane_play.log  # 只分析已有日志
  python3 plane_stat.py --ports /dev/cu.usbmodem21201   # 只监听一台

判定依据（固件日志）：
  item spawn kind=0/1/2        掉落了什么（0=火 1=弹 2=♥）
  item: FIRE level=N cols=M    吃到「火」：档位与列数（cols 应等于 1+level）
  item: LIFE +1 / full         吃到「生命」/ 命已满折算分数
  item: BOMB seq=N             吃到炸弹
  player hit -> life lost      受击扣命（有生命护航）
  BUILD=build_fixNN            固件版本
"""

import argparse
import collections
import re
import sys
import time

DEFAULT_PORTS = [
    ("A", "/dev/cu.usbmodem21201"),
    ("B", "/dev/cu.usbmodem21301"),
]
BAUD = 115200

KIND_NAME = {0: "火力(火)", 1: "炸弹(弹)", 2: "生命(♥)"}

RE_KIND = re.compile(r"item spawn kind=(\d+)")
RE_FIRE = re.compile(r"item: FIRE level=(\d+) cols=(\d+)")
RE_LIFE = re.compile(r"item: LIFE \+1 \(lives=(\d+)/(\d+)\)")
RE_LIFE_FULL = re.compile(r"item: LIFE full")
RE_BOMB = re.compile(r"item: BOMB seq=(\d+)")
RE_HIT = re.compile(r"player hit -> life lost")
RE_BUILD = re.compile(r"BUILD=(build_\w+)")
RE_BAD = re.compile(r"(ERROR|abort\(\)|Guru Meditation|assert failed|panic)")


class Stats(object):
    def __init__(self):
        self.spawn = collections.Counter()      # kind -> 次数
        self.pick_fire = 0
        self.pick_life = 0
        self.pick_life_full = 0
        self.pick_bomb = 0
        self.hits = 0
        self.fire_levels = []                   # 拾取「火」时的 level 序列
        self.cols_bad = []                      # cols != 1+level 的异常样本
        self.build = None
        self.bad = []

    def feed(self, line):
        m = RE_BUILD.search(line)
        if m:
            self.build = m.group(1)
        m = RE_KIND.search(line)
        if m:
            self.spawn[int(m.group(1))] += 1
        m = RE_FIRE.search(line)
        if m:
            lvl, cols = int(m.group(1)), int(m.group(2))
            self.pick_fire += 1
            self.fire_levels.append(lvl)
            if cols != 1 + lvl:
                self.cols_bad.append((lvl, cols))
        if RE_LIFE.search(line):
            self.pick_life += 1
        if RE_LIFE_FULL.search(line):
            self.pick_life_full += 1
        m = RE_BOMB.search(line)
        if m:
            self.pick_bomb += 1
        if RE_HIT.search(line):
            self.hits += 1
        if RE_BAD.search(line):
            self.bad.append(line.strip()[:160])

    def report(self):
        out = []
        total = sum(self.spawn.values())
        out.append("")
        out.append("=" * 62)
        out.append("固件版本        : %s" % (self.build or "未捕获（设备未重启/未进游戏）"))
        out.append("掉落道具总数    : %d" % total)
        if total:
            for k in sorted(KIND_NAME):
                n = self.spawn.get(k, 0)
                out.append("  %-10s : %3d  (%.1f%%)" % (KIND_NAME[k], n, 100.0 * n / total))
        out.append("拾取「火」次数  : %d   档位序列: %s" % (
            self.pick_fire, self.fire_levels if self.fire_levels else "-"))
        if self.fire_levels:
            out.append("  最高档位      : %d (=> %d 列子弹)" % (
                max(self.fire_levels), 1 + max(self.fire_levels)))
        out.append("拾取「生命」    : %d 次（含命满折算 %d 次）" % (
            self.pick_life + self.pick_life_full, self.pick_life_full))
        out.append("拾取「炸弹」    : %d 次" % self.pick_bomb)
        out.append("受击扣命        : %d 次" % self.hits)
        if self.cols_bad:
            out.append("⚠ 列数异常      : %s（应为 cols = 1 + level）" % self.cols_bad)
        if self.bad:
            out.append("⚠ 异常日志 %d 条:" % len(self.bad))
            for b in self.bad[:5]:
                out.append("    %s" % b)
        out.append("=" * 62)
        return "\n".join(out)


def analyze_log(path):
    st = Stats()
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            st.feed(line)
    print("分析日志：%s" % path)
    print(st.report())


def live(ports, duration):
    try:
        import serial
    except ImportError:
        print("缺少 pyserial：请用 /Users/zhangzuoliang/.workbuddy/binaries/python/"
              "versions/3.13.12/bin/python3 运行", file=sys.stderr)
        return 1

    sers = []
    for tag, port in ports:
        try:
            s = serial.Serial(port, BAUD, timeout=0.3)
            s.dtr = True      # 保持 True，避免打开串口时把板子复位
            s.rts = True
            sers.append((tag, s))
            print("已连接 %s %s" % (tag, port))
        except Exception as e:
            print("无法打开 %s %s: %s" % (tag, port, e), file=sys.stderr)
    if not sers:
        return 1

    st = Stats()
    print("开始监听 %d 秒，请在设备上进入「应用 -> 飞机」开玩（联机需两台都进）……" % duration)
    t0 = time.time()
    try:
        while time.time() - t0 < duration:
            for tag, s in sers:
                try:
                    line = s.readline()
                except Exception:
                    continue
                if not line:
                    continue
                txt = line.decode("utf-8", errors="replace")
                st.feed(txt)
                # 只把关键行回显出来，避免刷屏
                if (RE_KIND.search(txt) or RE_FIRE.search(txt) or RE_LIFE.search(txt) or
                        RE_LIFE_FULL.search(txt) or RE_BOMB.search(txt) or
                        RE_HIT.search(txt) or RE_BAD.search(txt)):
                    print("[%s %5.1fs] %s" % (tag, time.time() - t0, txt.strip()[:150]))
    except KeyboardInterrupt:
        print("\n（手动中断）")
    finally:
        for _, s in sers:
            try:
                s.close()
            except Exception:
                pass
    print(st.report())
    return 0


def main():
    ap = argparse.ArgumentParser(description="飞机游戏道具统计（build_fix44+）")
    ap.add_argument("-d", "--duration", type=float, default=90.0, help="监听秒数（默认 90）")
    ap.add_argument("--log", help="只分析已有日志文件，不连串口")
    ap.add_argument("--ports", nargs="*", help="自定义串口列表（默认 A=21201 B=21301）")
    args = ap.parse_args()

    if args.log:
        analyze_log(args.log)
        return 0
    if args.ports:
        ports = [("P%d" % i, p) for i, p in enumerate(args.ports)]
    else:
        ports = DEFAULT_PORTS
    return live(ports, args.duration)


if __name__ == "__main__":
    sys.exit(main())
