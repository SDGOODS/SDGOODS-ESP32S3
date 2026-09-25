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
"""检查「谷仓次元屏（SDGOODS-ESP32S3）」固件开发环境是否齐全。

这是二次开发的**第一道基础**：用 AI 助手（或自己）改代码前，先跑一遍本脚本，
它会逐项报出「缺什么、怎么装」，避免编译到一半才发现 ESP-IDF 没装、或改了中文
却没装 lv_font_conv 导致烧出满屏方框。

    python3 tools/check_env.py            # 人类可读表格 + 退出码
    python3 tools/check_env.py --json     # 给 AI / CI 解析的结构化输出

退出码：
    0  所有 MUST 项齐全（可以开始编译）
    1  有 MUST 项缺失（无法编译 / 烧录，必须先补齐）
    2  参数错误或无法运行

检查项分两级：
    MUST   缺了就编译不了 / 烧不了（ESP-IDF、python3、esptool、git）
    WARN   仅有特定需求才需要（改/增中文文案才需 node+npm 装 lv_font_conv）

注意：本机需联网，首次 `idf.py build` 会从组件管理器拉取 LVGL 8.3.11。
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

MIN_PYTHON = (3, 8)
# ESP-IDF 最低要求版本（本工程按 5.5 锁 LVGL 8.3.11）
MIN_IDF = (5, 5)
# lv_font_conv（font 工具链）需要 Node >= 16
MIN_NODE = (16, 0)


def _run(cmd, timeout=20):
    """跑一条命令，返回 (returncode, stdout.strip())；失败返回 (None, '')。"""
    try:
        p = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            text=True,
        )
        return p.returncode, (p.stdout or "").strip()
    except (FileNotFoundError, OSError):
        return None, ""
    except subprocess.TimeoutExpired:
        return None, ""


def _ver_tuple(s):
    """从 'v5.5.0' / 'Python 3.13.12' / 'v18.19.0' 之类字符串里取 (major,minor,patch)。"""
    m = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", s or "")
    if not m:
        return None
    return tuple(int(x) if x is not None else 0 for x in m.groups())


# 分步进度的标记（status -> 图标/文字）
_MARK = {"OK": "[OK] ", "FAIL": "[XX] ", "WARN": "[!!] ", "INFO": "[ii] "}


def _progress(stream, idx, total, name, status=None, detail=None):
    """往 stream 打一行进度。status 为空 = 开始；否则 = 该项结果。"""
    if stream is None:
        return
    if status is None:
        print("[%d/%d] %s ..." % (idx, total, name), file=stream, flush=True)
    else:
        first = (detail or "").split("\n")[0]
        print("%s[%d/%d] %-22s %s" % (_MARK.get(status, "[??]"), idx, total, name, first),
              file=stream, flush=True)


# ---------------------------------------------------------------------------
# 各项检查
# ---------------------------------------------------------------------------

def check_python():
    v = sys.version_info[:3]
    if v >= MIN_PYTHON:
        return "OK", "python3 %d.%d.%d" % v, ""
    return "FAIL", "python3 %d.%d.%d" % v, \
        "需要 >= %d.%d，请用系统包管理器或 pyenv 升级 Python。" % MIN_PYTHON


def find_idf():
    """返回 (idf.py 路径, 找到但未 source 的提示)。"""
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and os.path.isdir(idf_path):
        return os.path.join(idf_path, "tools", "idf.py"), None
    for cand in (os.path.expanduser("~/esp/esp-idf"),
                 "/opt/esp-idf", "/usr/local/esp-idf"):
        p = os.path.join(cand, "tools", "idf.py")
        if os.path.isfile(p):
            return p, "找到 ESP-IDF，但未设置 IDF_PATH；请先 `source %s/export.sh`" % cand
    which = shutil.which("idf.py")
    if which:
        return which, None
    return None, "未找到 ESP-IDF。安装：https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html"


def check_idf():
    idf_py, hint = find_idf()
    if not idf_py:
        return "FAIL", "未安装", hint or "缺少 ESP-IDF，无法编译。", True
    rc, out = _run([sys.executable, idf_py, "--version"])
    if rc != 0 or not out:
        # 能找到 idf.py 但跑不起来，多半是没 source 环境
        base = hint or "ESP-IDF 已存在但当前 shell 未初始化；请 `source <ESP-IDF>/export.sh` 后重试。"
        # 解包安装的 ESP-IDF（目录里没有 .git）用 export.sh 会静默失效，
        # 实测有效的替代是把 idf_tools.py export 的输出 eval 进当前 shell。
        return "FAIL", "已安装但未激活", \
            base + "\n若 export.sh 无效（解包版 ESP-IDF 无 .git，会静默失败）：\n" \
            "    eval \"$(python3 $IDF_PATH/tools/idf_tools.py export --format=key-value)\"\n" \
            "    export IDF_PATH=$HOME/esp/esp-idf   # 未设置时补上"
    m = re.search(r"ESP-IDF\s+v?(\d+\.\d+\.\d+)", out)
    ver = m.group(1) if m else "?"
    vt = _ver_tuple(ver)
    if vt and vt >= MIN_IDF:
        return "OK", "ESP-IDF %s" % ver, (hint or "idf.py 就绪"), False
    return "FAIL", "ESP-IDF %s" % ver, \
        "需要 >= %d.%d（本工程按 %d.%d 锁 LVGL 8.3.11）。请升级 ESP-IDF。" % (MIN_IDF + MIN_IDF), True


def check_esptool():
    for cmd in (["esptool.py", "--version"], ["esptool", "version"],
                [sys.executable, "-m", "esptool", "version"]):
        rc, out = _run(cmd)
        if rc == 0 and out:
            m = re.search(r"(\d+\.\d+\.\d+)", out)
            return "OK", "esptool %s" % (m.group(1) if m else "?"), \
                "烧录/合并固件需要。", False
    # ESP-IDF 安装后通常自带，只是没在 PATH
    return "WARN", "未在 PATH 找到", \
        "烧录需要 esptool；通常 ESP-IDF 自带，source 其 export.sh 后即可用。", False


def check_git():
    rc, out = _run(["git", "--version"])
    if rc == 0 and out:
        return "OK", out.replace("git ", "git "), "克隆/提交代码需要。", False
    return "WARN", "未安装", "仅克隆/提交时需要；不参与编译。", False


def check_node_npm():
    """改/增中文文案才需要（lv_font_conv 跑字体生成）。"""
    node = shutil.which("node")
    npm = shutil.which("npm")
    if node and npm:
        rc, out = _run(["node", "--version"])
        vt = _ver_tuple(out)
        if vt and vt >= MIN_NODE:
            return "OK", "node %s / npm %s" % (out, _run(["npm", "--version"])[1]), \
                "改中文文案时运行 `npm i lv_font_conv` 即可生成字体。", False
        return "WARN", "node %s（过旧）" % out, \
            "lv_font_conv 需要 Node >= %d.%d，请升级。" % MIN_NODE, False
    return "WARN", "未安装 node/npm", \
        "仅当你要改/增中文文案（重跑 tools/gen_fonts.py）时才需要：\n" \
        "    npm i lv_font_conv        # 装字体转换工具\n" \
        "不碰中文文案则无需安装，可直接编译。", False


def check_build_artifact():
    """信息项：是否已经编出固件。"""
    cands = ["build/SDGOODS_EBADGE.bin", "build/home_demo.bin"]
    for c in cands:
        if os.path.isfile(c):
            return "OK", c, "已编译，可直接烧录。", False
    return "INFO", "尚未编译", "运行 `idf.py build` 生成固件（首次会联网拉 LVGL）。", False


def check_serial_driver():
    """信息项：提示 USB 驱动（不同系统）。"""
    plat = sys.platform
    if plat == "darwin":
        return "INFO", "macOS", \
            "原生 USB-Serial-JTAG 免驱；若用 CP210x 转串口需装 SiLabs 驱动。\n" \
            "设备通常是 /dev/cu.usbmodem*，烧录前 `ls /dev/cu.usbmodem*` 确认。", False
    if plat.startswith("linux"):
        return "INFO", "Linux", \
            "可能需要把用户加入 dialout 组：sudo usermod -aG dialout $USER（注销重登）。\n" \
            "设备通常是 /dev/ttyACM* 或 /dev/ttyUSB*。", False
    return "INFO", "Windows", \
        "建议用 WSL2 + ESP-IDF；或装 CP210x / USB-Serial-JTAG 驱动，端口为 COMx。", False


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="检查 SDGOODS-ESP32S3 固件开发环境")
    ap.add_argument("--json", action="store_true", help="输出 JSON（供 AI/CI 解析）")
    ap.add_argument("--no-progress", action="store_true",
                    help="关闭分步进度输出（只给最终结论）")
    args = ap.parse_args()

    # 注意：这里存的是函数本身，不是调用结果 —— 延迟执行才能边跑边报进度。
    plan = [
        ("python3", "MUST", check_python),
        ("ESP-IDF", "MUST", check_idf),
        ("esptool", "MUST", check_esptool),
        ("git",     "WARN", check_git),
        ("node/npm（字体工具链）", "WARN", check_node_npm),
        ("编译产物", "INFO", check_build_artifact),
        ("烧录串口", "INFO", check_serial_driver),
    ]
    total = len(plan)
    # --json 时 stdout 必须只有 JSON，进度走 stderr；人类模式进度就在 stdout。
    out = None if args.no_progress else (sys.stderr if args.json else sys.stdout)

    if out is not None:
        print("SDGOODS-ESP32S3 开发环境检查：共 %d 项，通常 3-10 秒 "
              "（idf.py / esptool 首次冷启动各可能到 20 秒上限）" % total, file=out, flush=True)

    results = []
    n_fail = 0
    for idx, (name, level, fn) in enumerate(plan, 1):
        _progress(out, idx, total, name)
        status, detail, hint, *_rest = fn()
        _progress(out, idx, total, name, status, detail)
        results.append({"name": name, "level": level, "status": status,
                        "detail": detail, "hint": hint})
        if status == "FAIL":
            n_fail += 1

    if args.json:
        print(json.dumps({"fail": n_fail, "checks": results}, ensure_ascii=False, indent=2))
        return 1 if n_fail else 0

    # 人类可读：上面已逐行报过结果，这里只补 hint 与结论，避免重复刷屏
    if out is not None:
        pending = [r for r in results if r["hint"] and r["status"] != "OK"]
        if pending:
            print("-" * 56)
            print("待处理 / 提示：")
            for r in pending:
                print("  %s %s：%s" % (_MARK.get(r["status"], "").strip() or "[  ]",
                                       r["name"], r["detail"]))
                for line in r["hint"].split("\n"):
                    print("      %s" % line)
        print("-" * 56)
    if n_fail:
        print("✗ 有 %d 项 MUST 缺失，无法编译/烧录。先按上面提示补齐。" % n_fail)
        print("  详见 docs/ENVIRONMENT.md")
        return 1
    print("✓ 编译/烧录所需环境齐全。改中文文案前再确认 node/npm（见 WARN 项）。")
    print("  下一步：idf.py build  （或 python3 tools/new_app_project.py <name> 派生独立应用工程）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
