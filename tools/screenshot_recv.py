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
"""ESP32「一键截屏」接收端：把设备经 USB 串口发来的原始 RGB565 位图还原成 PNG。

固件侧：components/sdgoods_board/src/sdgoods_screenshot.c（串口收到 's' 即触发；
应用外壳菜单里的「截屏」按钮已移除，所以串口是设备端唯一的触发入口）。

传输协议（BEGIN/END 是文本行，中间的像素是**原始二进制**，不做任何编码）：
    ===SHOT-BEGIN w=360 h=360 bpp=16 fmt=1 swap=0 bytes=24567===
    <bytes 个 JPEG 字节（fmt=1）或原始 RGB565 字节（fmt=0），不靠换行分帧>
    ===SHOT-END===
    fmt=1：PC 端直接把字节落盘为 .jpg；fmt=0：PC 端转 PNG（旧固件无 fmt 字段，按 0 处理）。

为什么不用 base64：base64 让数据膨胀 33% 且每字节都要编解码，是过去 33 秒耗时
（约 10.5 KB/s）的主因。改成「文本头 + 定长原始二进制 + 文本尾」后，体积直接回到
259200 字节，设备端也省掉编码 CPU。二进制里可能含 0x0A/0x0D，所以像素部分**不靠
换行分帧**，而是靠 BEGIN 头里声明的 bytes 字段「精确读取那么多字节」，END 标记只
用于收尾校验——彻底避免了半行被当成整行、base64 被静默截短的老问题。

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

    # 截「非首屏」界面：先发一个切换字符（串口调试命令），等页面渲染好再截
    python3 screenshot_recv.py -p /dev/cu.usbmodem21301 --pre c -o cc.png

    # 2) 设备端：无需操作 —— 上面 -t 已自动向串口发触发字符 's'
    #    PNG 默认存到当前目录：shot_YYYYmmdd_HHMMSS.png

为什么需要 --pre
----------------
截屏只能拿到「当前显示的那一屏」。控制中心 / 二级页这类要靠触摸才能到的界面，
在固件里挂一个串口调试命令（例如启动器的 `c` 开控制中心）后用 --pre 先把它切出来，
等 --wait 秒让 LVGL 渲染完，再发 's' 截屏 —— 这是唯一能离屏核验非首屏 UI 的手段。
⚠️ 调试命令的回调跑在 console RX 任务里，固件侧必须用 lv_async_call() 转到 LVGL
线程再建对象，否则对象树会在错误线程被改。

自检（不需要设备，只验证解析 + RGB565 转换 + PNG 写出）
    python3 screenshot_recv.py --selftest

说明：脚本会保持 DTR/RTS 为高电平，避免打开串口时把设备复位（与工程里既有脚本一致）。

耗时：设备端先把 360×360 截屏用 JPEG 编码（典型 20~50KB），走 USB-Serial-JTAG（约
10.5 KB/s）实测约 **2~5 秒**；编码失败时退回原始 RGB565（259KB，约 25 秒）。别设太短的 --timeout。
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
    r'===SHOT-BEGIN\s+w=(\d+)\s+h=(\d+)\s+bpp=(\d+)\s+(?:fmt=(\d+)\s+)?swap=(\d+)\s+bytes=(\d+)==='
)
END_MARK = b'===SHOT-END==='
DEFAULT_PORT = '/dev/cu.usbmodem21201'
DEFAULT_BAUD = 115200


# --------------------------------------------------------------------------- 位图转换
def raw_to_rgb888(raw, w, h, swap):
    """原始 RGB565 字节串 -> RGB888 字节串。swap=1 表示内存里高字节在前（LV_COLOR_16_SWAP=1）。"""
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
    """按字节流解析一次截屏传输（文本头 + 定长原始二进制 + 文本尾）。

    非协议文本（ESP_LOG 启动日志、触发日志）在 BEGIN 出现前会被自动忽略；
    BEGIN 与 END 之间的像素是原始二进制，靠 bytes 字段精确截取，不依赖换行。
    """

    def __init__(self):
        self.reset()

    def reset(self):
        self.active = False        # 是否已看到 BEGIN
        self.w = self.h = self.swap = self.bytes_total = 0
        self.fmt = 0               # 0=原始 RGB565（PC 转 PNG）；1=JPEG（PC 直接落 .jpg）
        self.head = b''            # BEGIN 出现前的文本缓冲
        self.img = b''             # 已收集的原始图像字节
        self.tail = b''            # 图像之后、END 之前的尾随字节
        self.got_img = False

    def feed(self, chunk):
        """喂入一块字节，返回 None 或 ('complete', info)。

        info = dict(w, h, swap, raw, nbytes)
        """
        if not self.active:
            self.head += chunk
            idx = self.head.find(b'===SHOT-BEGIN')
            if idx < 0:
                if len(self.head) > 256:
                    self.head = self.head[-256:]
                return None
            nl = self.head.find(b'\n', idx)
            if nl < 0:                       # BEGIN 头行还没收全
                if len(self.head) > 512:
                    self.head = self.head[-512:]
                return None
            line = self.head[idx:nl].decode('ascii', 'replace')
            m = BEGIN_RE.match(line)
            if not m:
                self.head = self.head[nl + 1:]   # 畸形头，跳过继续找
                return None
            g = m.groups()
            self.w = int(g[0]); self.h = int(g[1]); _bpp = int(g[2])
            self.fmt = int(g[3]) if g[3] is not None else 0   # 旧固件无 fmt 字段，按 0 处理
            self.swap = int(g[4]); self.bytes_total = int(g[5])
            self.active = True
            self.img = b''
            self.tail = b''
            rest = self.head[nl + 1:]        # BEGIN 行之后的字节属于图像
            self.head = b''
            return self._feed_img(rest)
        return self._feed_img(chunk)

    def _feed_img(self, chunk):
        if not self.got_img:
            self.img += chunk
            if len(self.img) >= self.bytes_total:
                extra = self.img[self.bytes_total:]
                self.img = self.img[:self.bytes_total]
                self.got_img = True
                return self._feed_tail(extra)
            return None
        return self._feed_tail(chunk)

    def _feed_tail(self, chunk):
        self.tail += chunk
        if END_MARK in self.tail:
            if len(self.img) != self.bytes_total:
                return ('error', '图像字节数不符：收到 %d，期望 %d'
                        % (len(self.img), self.bytes_total))
            info = dict(w=self.w, h=self.h, swap=self.swap, fmt=self.fmt,
                        raw=bytes(self.img), nbytes=self.bytes_total)
            self.reset()
            return ('complete', info)
        if len(self.tail) > 256:             # END 还没来，别无限涨内存
            self.tail = self.tail[-256:]
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


def out_path_for(base, index, count, ext):
    if not base:
        return 'shot_%s%s' % (datetime.now().strftime('%Y%m%d_%H%M%S'), ext)
    if count <= 1:
        return base
    root, old = os.path.splitext(base)
    return '%s_%d%s' % (root, index, old or ext)


def run(port, baud, count, out, timeout_sec, trigger, pre=None, pre_wait=1.5):
    ser = open_port(port, baud)
    if ser is None:
        return 2
    try:
        ser.timeout = 0.2      # 按块读，超时设小一点好及时检查总超时
    except Exception:
        pass

    asm = ShotAssembler()
    shots = 0
    print('监听 %s（%d baud）' % (port, baud))
    print("等串口触发；用 -t 可让脚本自动向串口发 's'")

    # --pre：先发切换字符（固件里的串口调试命令），等页面渲染好，随后由下面的
    #        trigger 逻辑发 's' 截屏。这样才能截到控制中心 / 二级页这类非首屏 UI。
    if pre:
        trigger = True
        print("先发切换字符 %r，等 %.1fs 渲染后再截屏" % (pre, pre_wait))
        try:
            ser.write(pre.encode('ascii', 'replace'))
            ser.flush()
        except Exception as e:
            print('发送切换字符失败：%s' % e)
            return 4
        time.sleep(pre_wait)

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
                chunk = ser.read(8192)
            except Exception as e:
                print('读取串口出错：%s' % e)
                return 4
            if not chunk:
                continue
            result = asm.feed(chunk)
            if result is None:
                continue
            state, info = result
            if state == 'begin':
                pass    # 本协议里 begin 信息已在 feed 内部消费，不单独回传
            elif state == 'error':
                print('接收失败：%s' % info)
            elif state == 'complete':
                shots += 1
                if info['fmt'] == 1:
                    # JPEG：设备端已编码好，PC 端直接落盘，无需解码
                    jout = out
                    if jout and jout.lower().endswith('.png'):
                        jout = jout[:-4] + '.jpg'
                    path = out_path_for(jout, shots, count, '.jpg')
                    with open(path, 'wb') as f:
                        f.write(info['raw'])
                else:
                    try:
                        rgb = raw_to_rgb888(info['raw'], info['w'], info['h'], info['swap'])
                    except Exception as e:
                        print('解码失败：%s' % e)
                        shots -= 1
                        continue
                    path = out_path_for(out, shots, count, '.png')
                    write_png(path, info['w'], info['h'], rgb)
                print('已保存 #%d：%s （%dx%d，%.1f KB，%s）'
                      % (shots, os.path.abspath(path), info['w'], info['h'],
                         os.path.getsize(path) / 1024.0,
                         'JPEG' if info['fmt'] == 1 else 'PNG'))
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
    """不需要设备：造一张 360x360 测试图，走与设备完全相同的二进制协议与解码路径。"""
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

    stream = (b'===SHOT-BEGIN w=%d h=%d bpp=16 swap=%d bytes=%d===\n'
              % (w, h, swap, len(raw)))
    stream += bytes(raw)
    stream += b'===SHOT-END===\n'

    # 切成 1000 字节的小块喂进去，模拟真实串口分块到达（含跨 BEGIN/跨图像边界）
    asm = ShotAssembler()
    info = None
    for i in range(0, len(stream), 1000):
        r = asm.feed(stream[i:i + 1000])
        if r and r[0] == 'complete':
            info = r[1]
    if not info:
        print('自检失败：解析未完成')
        return 1
    rgb = raw_to_rgb888(info['raw'], info['w'], info['h'], info['swap'])
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

    # JPEG 分支：构造 fmt=1 的传输（负载用任意字节模拟 JPEG），验证接收端能按
    # fmt=1 正确收齐字节并落 .jpg（设备端真实 JPEG 的合法性由编码器保证，此处只验管线）。
    jraw = bytes((i * 7) & 0xFF for i in range(300))
    jstream = (b'===SHOT-BEGIN w=8 h=8 bpp=16 fmt=1 swap=0 bytes=%d===\n'
               % len(jraw))
    jstream += jraw + b'===SHOT-END===\n'
    asm2 = ShotAssembler()
    jinfo = None
    for i in range(0, len(jstream), 37):
        r = asm2.feed(jstream[i:i + 37])
        if r and r[0] == 'complete':
            jinfo = r[1]
    if not jinfo or jinfo['fmt'] != 1 or len(jinfo['raw']) != len(jraw):
        print('自检失败：JPEG 分支 fmt/字节数不符', jinfo)
        return 1
    jpath = out or 'shot_selftest.jpg'
    if os.path.abspath(jpath) == os.path.abspath(
            (out or 'shot_selftest.png')):
        jpath = 'shot_selftest_jpeg.jpg'
    with open(jpath, 'wb') as f:
        f.write(jinfo['raw'])
    print('自检（JPEG 分支）：%s 写入 %d 字节' % (os.path.abspath(jpath), len(jraw)))

    print('自检通过：RGB565 分支 + JPEG 分支 解析 / 落盘 全部正常')
    return 0


# --------------------------------------------------------------------------- 固件能力查询
def query_caps(port, baud, timeout_sec):
    """向串口发 '?'，解析固件回传的 'SDGOODS-CAPS:SHOT,...' 一行。

    成功则返回 0 并打印能力列表；超时/无响应也返回 0（此时提示旧固件可能不支持，
    可改用 -t 直接触发截图兜底）。与网页端 upload-firmware.html 的 queryCaps() 解析约定一致。
    """
    ser = open_port(port, baud)
    if ser is None:
        return 2
    try:
        ser.timeout = 0.5
    except Exception:
        pass
    print('查询固件能力：%s（%d baud）' % (port, baud))
    print("向串口发送 '?' ...")
    try:
        ser.write(b'?')
        ser.flush()
    except Exception as e:
        print('发送失败：%s' % e)
        return 4
    MARK = b'SDGOODS-CAPS:'
    buf = b''
    t0 = time.time()
    caps = None
    try:
        while True:
            if timeout_sec and (time.time() - t0) > timeout_sec:
                print('等待超时：%d 秒内未收到能力响应' % timeout_sec)
                break
            try:
                chunk = ser.read(256)
            except Exception as e:
                print('读取出错：%s' % e)
                return 4
            if not chunk:
                continue
            buf += chunk
            idx = buf.find(MARK)
            if idx >= 0:
                nl = buf.find(b'\n', idx)
                line = buf[idx:(nl if nl >= 0 else len(buf))].decode('ascii', 'replace')
                field = line[len(MARK):].strip()
                caps = [c for c in field.split(',') if c]
                break
            if len(buf) > 4000:      # 丢弃陈旧启动日志
                buf = buf[-1000:]
    finally:
        try:
            ser.close()
        except Exception:
            pass
    if caps is None:
        print('未收到能力响应（旧固件可能不支持查询；请直接用 -t 触发截图）')
        return 0
    if caps:
        print('固件能力：%s' % ', '.join(caps))
    else:
        print('固件能力：（空，未启用任何可选基础能力）')
    return 0


def main():
    ap = argparse.ArgumentParser(description='ESP32 一键截屏接收端（存为 PNG）')
    ap.add_argument('-p', '--port', default=DEFAULT_PORT, help='串口设备（默认 %s）' % DEFAULT_PORT)
    ap.add_argument('-b', '--baud', type=int, default=DEFAULT_BAUD, help='波特率（USB CDC 下无实际影响）')
    ap.add_argument('-o', '--out', default=None, help='输出 PNG 路径（默认 shot_时间戳.png）')
    ap.add_argument('-n', '--count', type=int, default=1, help='接收多少张后退出（默认 1）')
    ap.add_argument('-t', '--trigger', action='store_true', help="自动向串口发送 's' 触发截屏")
    ap.add_argument('--pre', default=None, metavar='CHAR',
                    help="先发送该切换字符（固件串口调试命令，如启动器的 c/d/b），等 --wait 秒后再触发截屏")
    ap.add_argument('--wait', type=float, default=1.5,
                    help='--pre 之后等待渲染的秒数（默认 1.5）')
    ap.add_argument('--caps', action='store_true', help="查询固件支持的基础能力（串口发 '?'）")
    ap.add_argument('--timeout', type=float, default=120.0, help='等待超时秒数（默认 120）')
    ap.add_argument('--selftest', action='store_true', help='不需要设备，自检解析与 PNG 写出')
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.out)
    if args.caps:
        return query_caps(args.port, args.baud, args.timeout)
    return run(args.port, args.baud, args.count, args.out, args.timeout, args.trigger,
               args.pre, args.wait)


if __name__ == '__main__':
    sys.exit(main())
