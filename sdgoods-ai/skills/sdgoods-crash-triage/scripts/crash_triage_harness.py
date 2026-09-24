#!/usr/bin/env python3
"""间歇性崩溃的「复现率二分」骨架（谷仓次元屏 SDGOODS / ESP32-S3）。

用法：把 CASES 改成你要对比的因子组合，然后
    PY="$HOME/.espressif/python_env/idf5.5_py3.13_env/bin/python"   # 需要 pyserial
    SDG_PORT=/dev/cu.usbmodemXXXXXX N=12 "$PY" crash_triage_harness.py [CASE_KEY]

需要 pyserial（用带 pyserial 的解释器跑，见上 PY）。SDG_PORT 未设时会自动探测
`ls /dev/cu.usbmodem*`，找不到则报错退出。

设计要点（都是踩过的坑，别改）：
  · **一次开串口跑完 N 轮**：反复开关串口会诱发幻触（USB-Serial-JTAG 老问题），
    也会让"复位"更不可靠。
  · 每轮**先复位**：否则记不清到底哪一轮崩的，也可能跑在上一轮遗留状态上。
  · **崩溃判据只看 `Guru Meditation` / `Rebooting...`**（不要拿"日志变少了"当判据）。
  · 控制组纪律：一次只留一个变量；再做一个"把可疑模块换成已知安全的等价实现"的实验。

⚠️ 复位：DTR 脉冲**并非每次生效**（实测同机前几轮有效、后几轮失效）。`Session.reset()`
   已经带「esptool 硬复位兜底 + 轮询确认」，确认失败会打印 `⚠️ 复位未生效`，
   这时的结果**不可信**——重跑或改用 esptool 路径。
"""
import glob
import os
import subprocess
import sys
import time

# 本脚本所在目录上溯两级 = 仓库根（skills/sdgoods-crash-triage/scripts/ 之上）
_REPO = os.environ.get(
    'SDG_ROOT',
    os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..')),
)
sys.path.insert(0, os.path.join(_REPO, 'tools'))
try:
    import screenshot_recv as sr   # noqa: E402   (提供 open_port)
except ImportError:
    sys.stderr.write(
        '找不到 tools/screenshot_recv.py。请设置 SDG_ROOT 指向含 tools/ 的仓库根。\n')
    raise


def _find_port():
    p = os.environ.get('SDG_PORT')
    if p:
        return p
    ports = glob.glob('/dev/cu.usbmodem*')
    if not ports:
        sys.stderr.write('未找到串口设备，请用 SDG_PORT=/dev/cu.usbmodemXXXXXX 指定。\n')
        raise SystemExit(2)
    return ports[0]


PORT = _find_port()
N = int(os.environ.get('N', '8'))


class Session:
    def __init__(self):
        self.ser = sr.open_port(PORT, 115200)
        if self.ser is None:
            raise SystemExit(2)
        self.ser.timeout = 0.2
        self.ser.rts = False
        self.ser.dtr = False
        time.sleep(0.2)
        self.buf = b''

    # ---- 唯一的读取出口：日志与"等 marker"共用它 ----
    def drain(self, sec):
        end = time.time() + sec
        while time.time() < end:
            d = self.ser.read(8192)
            if d:
                self.buf += d
        return self.buf.decode('utf-8', 'replace')

    def send(self, s, wait=1.2):
        self.ser.write(s if isinstance(s, bytes) else s.encode())
        self.ser.flush()
        self.drain(wait)

    def reset(self, settle=10.0):
        """复位并确认真的重启了；确认不了就打印警告（结果不可信）。"""
        # ① esptool 硬复位兜底（比 DTR 脉冲可靠；代价是端口会关一次）
        subprocess.run(['python3', '-m', 'esptool', '--chip', 'esp32s3', '-p', PORT,
                        '--before', 'default_reset', '--after', 'hard_reset', 'flash_id'],
                       capture_output=True)
        for _ in range(3):
            self.buf = b''
            self.ser.reset_input_buffer()
            # ② DTR 脉冲（串口保持打开，能抓到 bootloader 日志）
            self.ser.dtr = False
            time.sleep(0.05)
            self.ser.dtr = True
            time.sleep(0.10)
            self.ser.dtr = False
            t = self.drain(settle)
            if any(k in t for k in ('SDGOODS', 'boot: ESP-IDF', 'self-check', 'app_main')):
                return True
        print('        ⚠️ 复位未生效（uptime 会一路涨）—— 本轮结果不可信')
        return False

    def alive(self):
        t = self.buf.decode('utf-8', 'replace')
        return not ('Guru Meditation' in t or 'Rebooting...' in t)

    def close(self):
        self.ser.close()


# ===================== 改这里：一次只留一个变量 =====================

def case_a(s):
    """控制组 A：只做「不碰可疑模块」的最小动作。"""
    for _ in range(N):
        s.send('X', 0.4)
        s.send('===SLOT-BEGIN -1 0 -\n', 1.0)      # 坏头部 ⇒ 直接失败，不触发事件
        if not s.alive():
            return 'CRASH'
    return 'OK'


def case_b(s):
    """控制组 B：走「已知安全」的等价入口（老调试键），做同样的 UI 动作。"""
    for _ in range(N):
        s.send('q', 1.5)                            # 开浮层
        if not s.alive():
            return 'CRASH'
        s.send('Q', 0.8)                            # 关浮层
    return 'OK'


def case_c(s):
    """实验组 C：A 的上下文 + B 的 UI 动作 —— 崩了就说明差异在「上下文」而不是 UI。"""
    for _ in range(N):
        s.send('X', 0.4)
        s.send('===SLOT-BEGIN -1 999999 -\n', 2.0)  # 触发「槽满/失败」路径的事件通知
        if not s.alive():
            return 'CRASH'
        s.send('Q', 0.8)
    return 'OK'


CASES = [('A 只走通道（无事件）', case_a),
         ('B 只走老入口（有浮层）', case_b),
         ('C 通道 + 事件 + 浮层', case_c)]


def main() -> int:
    only = sys.argv[1].upper() if len(sys.argv) > 1 else None
    for key, fn in CASES:
        if only and not key.startswith(only):
            continue
        s = Session()
        ok_reset = s.reset()
        verdict = fn(s)
        s.drain(6.0)
        tail = s.buf.decode('utf-8', 'replace')
        flag = '💥 崩溃' if verdict == 'CRASH' else '✅ 稳定'
        print(f'  [{key}]  {N} 轮 → {flag}'
              f'{"" if ok_reset else "  (复位未确认)"}')
        if verdict == 'CRASH':
            i = tail.find('Guru')
            if i < 0:
                i = tail.find('Rebooting')
            for line in tail[max(0, i - 400):i + 500].splitlines():
                if line.strip():
                    print('        ', line.strip())
        s.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
