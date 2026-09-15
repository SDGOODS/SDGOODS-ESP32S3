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
"""ESP32「一键截屏」接收端：把设备经 USB 串口发来的 base64 位图还原成 PNG。

固件侧：main/screenshot.c（顶部下滑「菜单」-> 点「截屏」，或串口收到 's' 触发）。

用法
----
    # 1) 先运行本脚本（它要占用串口，所以请先关掉 idf.py monitor / 串口助手）
    #    默认监听 A 机 /dev/cu.usbmodem21201，抓 1 张
    python3 screenshot_recv.py

    # 自动发送触发字符 's' 并接收（不用碰设备）
    python3 screenshot_recv.py -t

    # 抓 B 机、连续抓 3 张
    python3 screenshot_recv.py -p /dev/cu.usbmodem21301 -n 3

    # 指定输出文件名 / 目录
    python3 screenshot_recv.py -o my_shot.png

    # 2) 设备端：顶部下滑打开「菜单」-> 点「截屏」
    #    PNG 默认存到当前目录：shot_YYYYmmdd_HHMMSS.png

自检（不需要设备，只验证解析 + RGB565 转换 + PNG 写出）
    python3 screenshot_recv.py --selftest

说明：脚本会保持 DTR/RTS 为高电平，避免打开串口时把设备复位（与工程里既有脚本一致）。
"""

import argparse
import base64
import os
import re
import struct
import sys
import time
import zlib
from datetime import datetime

BEGIN_RE = re.compile(
    r'===SHOT-BEGIN\s+w=(\d+)\s+h=(\d+)\s+bpp=(\d+)\s+swap=(\d+)\s+bytes=(\d+)\s+lines=(\d+)==='
)
END_MARK = '===SHOT-END==='
DATA_RE = re.compile(r'^([0-9A-Fa-f]{4}):([A-Za-z0-9+/=]+)$')

DEFAULT_PORT = '/dev/cu.usbmodem21201'
DEFAULT_BAUD = 115200


# --------------------------------------------------------------------------- 位图转换
def b64_to_rgb888(b64_text, w, h, swap):
    """base64(RGB565) -> RGB888 字节串。swap=1 表示内存里高字节在前（LV_COLOR_16_SWAP=1）。"""
    raw = base64.b64decode(b64_text)
    need = w * h * 2
    if len(raw) != need:
        raise ValueError('位图字节数不符：收到 %d，期望 %d' % (len(raw), need))
    out = bytearray(w * h * 3)
    for i in range(w * h):
        b0 = raw[2 * i]
        b1 = raw[2 * i + 1]
        v = (b0 << 8) | b1 if swap else b0 | (b1 << 8)
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        o = 3 * i
        out[o] = (r5 * 255 + 15) // 31
        out[o + 1] = (g6 * 255 + 31) // 63
        out[o + 2] = (b5 * 255 + 15) // 31
    return bytes(out)


def write_png(path, w, h, rgb):
    """写出 8bit RGB PNG（只用标准库，不依赖 Pillow）。"""
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # 每行滤波器类型 0 = None
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


# --------------------------------------------------------------------------- 协议解析
class ShotAssembler:
    """按行解析一次截屏传输；非协议行（ESP_LOG 日志）自动忽略。"""

    def __init__(self):
        self.reset()

    def reset(self):
        self.active = False
        self.w = self.h = self.swap = self.bytes_total = self.lines_total = 0
        self.lines = {}

    def feed(self, line):
        """喂入一行，返回 None 或 (state, info)。
        state: 'begin' | 'complete' | 'error'"""
        line = line.strip('\r\n')
        if not line:
            return None

        m = BEGIN_RE.match(line)
        if m:
            self.active = True
            (self.w, self.h, _bpp, self.swap,
             self.bytes_total, self.lines_total) = (int(g) for g in m.groups())
            self.lines = {}
            return ('begin', None)

        if not self.active:
            return None

        if line == END_MARK:
            self.active = False
            missing = [i for i in range(self.lines_total) if i not in self.lines]
            if missing:
                return ('error', '缺 %d/%d 行（有日志行混入或串口丢数据，请重试）'
                        % (len(missing), self.lines_total))
            seq = ''.join(self.lines[i] for i in range(self.lines_total))
            return ('complete', dict(w=self.w, h=self.h, swap=self.swap,
                                     b64=seq, nbytes=self.bytes_total))

        m = DATA_RE.match(line)
        if m:
            idx = int(m.group(1), 16)
            if idx < self.lines_total:
                self.lines[idx] = m.group(2)
        # 其它行（日志）忽略
        return None


# --------------------------------------------------------------------------- 串口接收
def open_port(port, baud):
    try:
        import serial
    except ImportError:
        print('需要 pyserial，请用带 pyserial 的解释器运行，例如：', file=sys.stderr)
        print('  /Users/zhangzuoliang/.workbuddy/binaries/python/versions/3.13.12/bin/python3 %s'
              % os.path.basename(__file__), file=sys.stderr)
        print('  或先安装：python3 -m pip install pyserial', file=sys.stderr)
        return None
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print('打开串口 %s 失败：%s' % (port, e), file=sys.stderr)
        print('提示：请先关闭 idf.py monitor / 串口助手；端口号可用 `ls /dev/cu.usbmodem*` 查看',
              file=sys.stderr)
        return None
    # 保持 EN 高电平，避免打开串口瞬间把设备复位（与工程里既有脚本一致）
    try:
        ser.dtr = True
        ser.rts = True
    except Exception:
        pass
    return ser


def out_path_for(base, index, count):
    if not base:
        return 'shot_%s.png' % datetime.now().strftime('%Y%m%d_%H%M%S')
    if count <= 1:
        return base
    root, ext = os.path.splitext(base)
    return '%s_%d%s' % (root, index, ext or '.png')


def run(port, baud, count, out, timeout_sec, trigger):
    ser = open_port(port, baud)
    if ser is None:
        return 2

    asm = ShotAssembler()
    shots = 0
    print('监听 %s（%d baud）' % (port, baud))
    print('请按设备「菜单」->「截屏」，或用 -t 让脚本自动触发')

    t0 = time.time()
    last_trig = 0.0
    try:
        while shots < count:
            # -t：每 5 秒重发一次触发（设备可能正在开机/忙），一旦开始接收就停发
            if trigger and not asm.active and (time.time() - last_trig) > 5.0:
                try:
                    ser.write(b's')
                    ser.flush()
                    last_trig = time.time()
                    print("已发送触发字符 's'")
                except Exception as e:
                    print('发送触发字符失败：%s' % e)
                    return 4
            if timeout_sec and (time.time() - t0) > timeout_sec:
                print('等待超时：%d 秒内没有收到完整截屏' % timeout_sec)
                return 3
            try:
                line = ser.readline()
            except Exception as e:
                print('读取串口出错：%s' % e)
                return 4
            if not line:
                continue
            result = asm.feed(line.decode('utf-8', 'replace'))
            if result is None:
                continue
            state, info = result
            if state == 'begin':
                print('开始接收：%dx%d swap=%d，%d 字节 / %d 行'
                      % (asm.w, asm.h, asm.swap, asm.bytes_total, asm.lines_total))
            elif state == 'error':
                print('接收失败：%s' % info)
            elif state == 'complete':
                shots += 1
                try:
                    rgb = b64_to_rgb888(info['b64'], info['w'], info['h'], info['swap'])
                except Exception as e:
                    print('解码失败：%s' % e)
                    shots -= 1
                    continue
                path = out_path_for(out, shots, count)
                write_png(path, info['w'], info['h'], rgb)
                print('已保存 #%d：%s （%dx%d，%.1f KB）'
                      % (shots, os.path.abspath(path), info['w'], info['h'],
                         os.path.getsize(path) / 1024.0))
                t0 = time.time()
                if trigger and shots < count:
                    time.sleep(0.3)
                    ser.write(b's')
                    ser.flush()
    finally:
        try:
            ser.close()
        except Exception:
            pass
    return 0


# --------------------------------------------------------------------------- 自检
def selftest(out):
    """不需要设备：造一张 360x360 测试图，走与设备完全相同的协议与解码路径。"""
    w = h = 360
    swap = 1                      # 设备 LV_COLOR_16_SWAP=1，高字节在前
    raw = bytearray()
    for y in range(h):
        for x in range(w):
            if 20 <= x < 120 and 20 <= y < 120:
                v = 0xF800        # 红
            elif 140 <= x < 240 and 20 <= y < 120:
                v = 0x07E0        # 绿
            elif 20 <= x < 120 and 140 <= y < 240:
                v = 0x001F        # 蓝
            elif 140 <= x < 240 and 140 <= y < 240:
                v = 0xFFE0        # 黄
            else:
                v = ((x * 31 // (w - 1)) << 11) | (((y * 63 // (h - 1)) & 0x3F) << 5) | 0x8
            raw += bytes([(v >> 8) & 0xFF, v & 0xFF]) if swap else bytes([v & 0xFF, (v >> 8) & 0xFF])

    per = 57
    lines = [bytes(raw[o:o + per]) for o in range(0, len(raw), per)]
    stream = ['===SHOT-BEGIN w=%d h=%d bpp=16 swap=%d bytes=%d lines=%d===\n'
              % (w, h, swap, len(raw), len(lines))]
    stream += ['%04X:%s\n' % (i, base64.b64encode(b).decode()) for i, b in enumerate(lines)]
    stream.append('===SHOT-END===\n')

    asm = ShotAssembler()
    info = None
    for line in stream:
        r = asm.feed(line)
        if r and r[0] == 'complete':
            info = r[1]
    if not info:
        print('自检失败：解析未完成')
        return 1
    rgb = b64_to_rgb888(info['b64'], info['w'], info['h'], info['swap'])
    path = out or 'shot_selftest.png'
    write_png(path, info['w'], info['h'], rgb)
    # 校验几个采样点的颜色是否符合预期（红/绿/蓝/黄）
    def px(x, y):
        o = (y * w + x) * 3
        return (rgb[o], rgb[o + 1], rgb[o + 2])
    checks = [((60, 60), (255, 0, 0)), ((180, 60), (0, 255, 0)),
              ((60, 180), (0, 0, 255)), ((180, 180), (255, 255, 0))]
    bad = [(p, px(*p), want) for p, want in checks if px(*p) != want]
    print('自检：%s （%dx%d，%.1f KB）'
          % (os.path.abspath(path), w, h, os.path.getsize(path) / 1024.0))
    if bad:
        print('自检失败，采样点颜色不符：%s' % bad)
        return 1
    print('自检通过：解析 / base64 / RGB565->RGB888 / PNG 写出 全部正常')
    return 0


def main():
    ap = argparse.ArgumentParser(description='ESP32 一键截屏接收端（存为 PNG）')
    ap.add_argument('-p', '--port', default=DEFAULT_PORT, help='串口设备（默认 %s）' % DEFAULT_PORT)
    ap.add_argument('-b', '--baud', type=int, default=DEFAULT_BAUD, help='波特率（USB CDC 下无实际影响）')
    ap.add_argument('-o', '--out', default=None, help='输出 PNG 路径（默认 shot_时间戳.png）')
    ap.add_argument('-n', '--count', type=int, default=1, help='接收多少张后退出（默认 1）')
    ap.add_argument('-t', '--trigger', action='store_true', help="自动向串口发送 's' 触发截屏")
    ap.add_argument('--timeout', type=float, default=120.0, help='等待超时秒数（默认 120）')
    ap.add_argument('--selftest', action='store_true', help='不需要设备，自检解析与 PNG 写出')
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.out)
    return run(args.port, args.baud, args.count, args.out, args.timeout, args.trigger)


if __name__ == '__main__':
    sys.exit(main())
