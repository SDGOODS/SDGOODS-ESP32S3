#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把「写 flash 期间静默触摸」这套修复同步到各个派生工程。

🔴 为什么不能整份覆盖：15 个派生工程的 `sdgoods_input.c` 实际有 **7 个不同版本**
   （滚动阈值 / 幻触滤除 / 读失败保持 / 轮询任务+总线锁 各自改过）。整份覆盖会
   凭空抹掉那些改动 —— 而且编译能过，静默丢功能，是最坏的一类错。

🔴🔴 为什么必须**先备份**：这些工程并不都在版本控制里。实测 `2026-09-11-16-40-47`
   根目录虽然是个 git 仓库，但**一个文件都没跟踪**（`2046/`、`HELLOAPP/` 全是 `??`）；
   而 BOOGOO 的 `sdgoods_input.c` 有未提交改动 —— 一次 `git checkout --` 就把它冲掉了，
   git 对象库里也没有副本（悬空提交里没这个文件、`tmutil` 无本地快照）。
   所以本脚本每改一个文件先把原件存到 `--backup-dir`（默认 /tmp/sdg-touch-sync/<时间戳>），
   并写 `manifest.tsv`（工程 / 文件 / 原 md5），可 `--restore` 整批还原。

用法（默认 dry-run，只报锚点命中情况，不改文件）：
    python3 tools/sync_touch_fix.py                          # 检查全部
    python3 tools/sync_touch_fix.py --apply 2046 CALC        # 写入指定工程
    python3 tools/sync_touch_fix.py --apply --all
    python3 tools/sync_touch_fix.py --restore /tmp/sdg-touch-sync/20261102-140000

判据（每个工程都必须过，否则一条都不写）：
    ① 每处锚点在目标文件里出现**恰好一次** —— 0 次 = 版本不认识，2 次 = 锚点不唯一
    ② 目标文件里原本没有 `sdgoods_touch_flash_window` —— 有就跳过（幂等）
    ③ 目标定义了 TAG 时复用它的 TAG，不重复定义
    ④ 应用后自检：TAG 只定义一次、CST816_ADDR 只定义一次、
       静默判断出现在第一次 `cst816_read(CST816_REG_DATA)` **之前**
"""
import argparse
import os
import re
import shutil
import sys
import time

SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BACKUP = "/tmp/sdg-touch-sync"

DERIVED = [
    "2046", "AUDIOPOD", "BADGE", "BOOGOO", "CALC", "DRAGONRADAR", "GLOSS",
    "HELLOAPP", "MARAGOTCHI4U", "MEETSPET", "PIXMON", "PUMPKIN3D",
    "SDGOODS-ESP32S3-gitee", "SDGOODS-PLANE", "TAMAP1",
]

REL_H = "components/sdgoods_board/include/sdgoods_input.h"
REL_C = "components/sdgoods_board/src/sdgoods_input.c"

TAG_LINE = '#define TAG                 "touch"\n'
TRON_ANCHOR = "#define CST816_TRON_MS"
TIMER_INC = '#include "esp_timer.h"          /* esp_timer_get_time：写盘窗口与慢读计时 */\n'
I2C_INC = '#include "driver/i2c.h"\n'
LOG_INC = '#include "esp_log.h"            /* 写盘窗口 / 慢读退避必须出声：验收唯一的正面证据 */\n'


class Skip(Exception):
    """已经同步过 / 版本不认识 —— 不是错误，但要明确说出来。"""


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write(p, s):
    with open(p, "w", encoding="utf-8") as f:
        f.write(s)


def once(s, needle, what):
    n = s.count(needle)
    if n != 1:
        raise Skip("锚点「%s」出现 %d 次（要求恰好 1）" % (what, n))
    return s.index(needle)


# ---------------------------------------------------------------------------
# 从修复后的源文件里抽出补丁片段（抽出来的是同一份字节，不存在手抄走形）
# ---------------------------------------------------------------------------
def extract_blocks():
    src = read(os.path.join(SRC_ROOT, REL_C))

    # block：从超时注释到 _end() 函数结束（**不含**前面的 #define TAG / CST816_*）
    i = src.index("/* 单次读 I2C 的超时。")
    j = src.index("\nstatic lv_indev_drv_t s_indev_drv;", i)
    block = src[i:j].rstrip("\n") + "\n"
    for must in ("#define CST816_RD_TIMEOUT_MS", "SDGOODS_TOUCH_FLASH_GRACE_MS",
                 "void sdgoods_touch_flash_window(int ms)", "_end(void)"):
        if must not in block:
            raise AssertionError("抽取的 block 不完整，缺 %r" % must)

    # guard：touchpad_read 开头的静默判断 + t0 计时起点
    i = src.index("    /* 🔴🔴 静默判断必须在")
    tail = "    const int64_t t0 = esp_timer_get_time();\n"
    j = src.index(tail, i) + len(tail)
    guard = src[i:j]
    if "if (touch_quiet_busy())" not in guard:
        raise AssertionError("抽取的 guard 不完整")

    # slow：读 + 计时 + 慢读退避（替换掉原来的单行 if）
    i = src.index("    const esp_err_t rd = cst816_read(CST816_REG_DATA, buf, sizeof(buf));")
    j = src.index("    if (rd != ESP_OK) {", i)
    slow = src[i:j]
    if "s_slow_run" not in slow:
        raise AssertionError("抽取的 slow 段不完整")

    return block, guard, slow


def patch_header(path, *, apply):
    src = read(path)
    if "sdgoods_touch_flash_window" in src:
        raise Skip("头文件已含声明")
    fixed = read(os.path.join(SRC_ROOT, REL_H))
    i = fixed.index("\n/* ============================ ⚠️ 写 flash 前必调")
    if not src.endswith("\n"):
        src += "\n"
    if apply:
        write(path, src + fixed[i:])
    return src + fixed[i:]


def _common_head(s, block):
    """两种变体共用的前半段：补 include、插 TAG + 静默窗口整块、改读超时。"""
    # ---- ① esp_log.h ----
    if '#include "esp_log.h"' not in s:
        i = once(s, I2C_INC, "include driver/i2c.h")
        s = s[:i] + I2C_INC + LOG_INC + s[i + len(I2C_INC):]

    # ---- ② esp_timer.h ----
    if '#include "esp_timer.h"' not in s:
        i = s.index('#include "esp_log.h"')
        eol = s.index("\n", i) + 1
        s = s[:eol] + TIMER_INC + s[eol:]

    # ---- ③ TAG + 静默窗口整块：插在 CST816_TRON_MS 之后 ----
    i = once(s, TRON_ANCHOR, "CST816_TRON_MS 定义")
    eol = s.index("\n", i) + 1
    has_tag = re.search(r"^[ \t]*#define[ \t]+TAG[ \t]", s, re.M) is not None
    tagpart = "" if has_tag else TAG_LINE + "\n"
    rest = s[eol + 1:] if s[eol:eol + 1] == "\n" else s[eol:]
    new = s[:eol] + "\n" + tagpart + block + "\n" + rest
    if new.count(TRON_ANCHOR) != s.count(TRON_ANCHOR):
        raise AssertionError("插入后 CST816_TRON_MS 数量变了（重复插入）")
    s = new

    # ---- ④ 读超时 50 -> 常量 ----
    if "pdMS_TO_TICKS(50)" in s:
        s = s.replace("pdMS_TO_TICKS(50)", "pdMS_TO_TICKS(CST816_RD_TIMEOUT_MS)", 1)
    return s


# 读被拆成 touch_hw_read() 的变体专用守卫：必须落在 touch_hw_read 里 ——
# `sdgoods_touch_poll_now()` 直接调它，守卫若在外层 touchpad_read，轮询型 app 完全绕过。
GUARD_POLLNOW = """    /* 🔴🔴 静默判断必须在**第一次 I2C 事务之前** —— 整个防护的落点。
     * ⚠️ 本工程把读拆成了 touch_hw_read()（LVGL 回调和 `sdgoods_touch_poll_now()` 共用），
     *    所以守卫必须放在**这一层**：放在外层 touchpad_read 的话，轮询型 app 一起绕过窗口。 */
    if (touch_quiet_busy()) {
        if (s_quiet_release) {
            /* 写盘窗口的第一次：正按着也补一次松手，否则 UI 卡在按下态。 */
            s_quiet_release = false;
            s_touch_state = LV_INDEV_STATE_RELEASED;
            s_win_pauses++;
            if (s_win_pauses == 1 || (s_win_pauses % 20) == 0) {
                ESP_LOGI(TAG, "flash write window -> touch paused (x%u)",
                         (unsigned)s_win_pauses);
            }
            return LV_INDEV_STATE_RELEASED;
        }
        /* 慢读退避：**沿用上次状态**，一个事务都不发。不能在这里补 RELEASED ——
         * 那会把"正按着"误判成"松手"，长按 / 游戏按住会被切断。 */
        if (out_x) {
            *out_x = s_touch_point.x;
        }
        if (out_y) {
            *out_y = s_touch_point.y;
        }
        s_backoff_pauses++;
        return s_touch_state;
    }
"""


def patch_source_pollnow(path, block, slow, *, apply):
    s = read(path)
    if "sdgoods_touch_flash_window" in s:
        raise Skip("源文件已含实现")
    s = _common_head(s, block)

    anchor = "static lv_indev_state_t touch_hw_read(lv_coord_t *out_x, lv_coord_t *out_y)\n{\n"
    i = once(s, anchor, "touch_hw_read 函数头")
    s = s[:i + len(anchor)] + GUARD_POLLNOW + s[i + len(anchor):]

    old_read = ("    uint8_t buf[5] = {0};\n"
                "    if (cst816_read(CST816_REG_DATA, buf, sizeof(buf)) != ESP_OK) {\n")
    i = once(s, old_read, "原始读判据")
    # ⚠️ 本变体的 GUARD_POLLNOW 不带 t0（它只返回值、没有 data 参数），
    #    计时起点必须在这里补 —— 漏了就是 `t0` 未声明，编译直接报错。
    s = s[:i] + ("    const int64_t t0 = esp_timer_get_time();\n"
                 "    uint8_t buf[5] = {0};\n") + slow + "    if (rd != ESP_OK) {\n" + \
        s[i + len(old_read):]

    n_tag = len(re.findall(r"^[ \t]*#define[ \t]+TAG[ \t]", s, re.M))
    if n_tag != 1 or s.count("#define CST816_ADDR") != 1:
        raise AssertionError("自检失败：TAG=%d CST816_ADDR=%d" % (n_tag, s.count("#define CST816_ADDR")))
    i_guard = s.index("if (touch_quiet_busy())")
    i_i2c = s.index("cst816_read(CST816_REG_DATA")
    i_fn = s.index("static lv_indev_state_t touch_hw_read(")
    if not (i_fn < i_guard < i_i2c):
        raise AssertionError("自检失败：守卫没落在 touch_hw_read 里 / 不在 I2C 之前")
    if apply:
        write(path, s)
    return s


def patch_source(path, block, guard, slow, *, apply):
    s = read(path)
    if "sdgoods_touch_flash_window" in s:
        raise Skip("源文件已含实现")
    # 🔴 读被拆成 touch_hw_read()（LVGL 回调 + 轮询型 app 共用）的工程走专用变体：
    #    真正的 I2C 事务在 touch_hw_read 里，而 sdgoods_touch_poll_now() 直接调它。
    #    守卫若插在外层 touchpad_read，轮询路径完全没有防护 —— 编译能过、功能静默缺失。
    if "touch_hw_read" in s:
        return patch_source_pollnow(path, block, slow, apply=apply)

    s = _common_head(s, block)

    # ---- ⑤ touchpad_read 开头守卫 ----
    i = once(s, "    (void)drv;\n", "(void)drv;")
    eol = i + len("    (void)drv;\n")
    rest = s[eol + 1:] if s[eol:eol + 1] == "\n" else s[eol:]
    s = s[:eol] + "\n" + guard + rest

    # ---- ⑥ 读判据拆成 rd + 计时 ----
    old_read = ("    uint8_t buf[5] = {0};\n"
                "    if (cst816_read(CST816_REG_DATA, buf, sizeof(buf)) != ESP_OK) {\n")
    i = once(s, old_read, "原始读判据")
    s = s[:i] + "    uint8_t buf[5] = {0};\n" + slow + "    if (rd != ESP_OK) {\n" + \
        s[i + len(old_read):]

    # ---- 自检：宁可当场炸，也不要留个「编译能过但结构错了」的文件 ----
    n_tag = len(re.findall(r"^[ \t]*#define[ \t]+TAG[ \t]", s, re.M))
    n_addr = s.count("#define CST816_ADDR")
    if n_tag != 1:
        raise AssertionError("自检失败：TAG 定义了 %d 次" % n_tag)
    if n_addr != 1:
        raise AssertionError("自检失败：CST816_ADDR 定义了 %d 次" % n_addr)
    i_guard = s.index("if (touch_quiet_busy())")
    i_i2c = s.index("cst816_read(CST816_REG_DATA")
    if not (i_guard < i_i2c):
        raise AssertionError("自检失败：静默判断落在 I2C 事务之后")
    if apply:
        write(path, s)
    return s


def do_one(root, proj, blocks, *, apply, backup_dir):
    base = os.path.join(root, proj)
    if not os.path.isdir(base):
        return "missing", "目录不存在"
    files = [os.path.join(base, REL_H), os.path.join(base, REL_C)]
    try:
        # 先在内存里全部算完再落盘 —— 中途发现锚点不认，一个字节都不写
        new_h = patch_header(files[0], apply=False)
        new_c = patch_source(files[1], *blocks, apply=False)
    except Skip as e:
        return "skip", str(e)
    except AssertionError as e:
        return "fail", str(e)
    if apply:
        dst = os.path.join(backup_dir, proj)
        os.makedirs(dst, exist_ok=True)
        import hashlib
        for f in files:
            os.makedirs(os.path.join(dst, os.path.dirname(os.path.relpath(f, base))), exist_ok=True)
            shutil.copy2(f, os.path.join(dst, os.path.relpath(f, base)))
            with open(os.path.join(backup_dir, "manifest.tsv"), "a", encoding="utf-8") as mf:
                mf.write("%s\t%s\t%s\n" % (proj, os.path.relpath(f, base),
                                           hashlib.md5(open(f, "rb").read()).hexdigest()))
        write(files[0], new_h)
        write(files[1], new_c)
    return ("written" if apply else "ready"), ""


def do_restore(backup_dir):
    man = os.path.join(backup_dir, "manifest.tsv")
    if not os.path.isfile(man):
        print("找不到 %s" % man)
        return 1
    root = os.path.dirname(SRC_ROOT)
    n = 0
    for line in open(man, encoding="utf-8"):
        proj, rel, md5 = line.rstrip("\n").split("\t")
        src = os.path.join(backup_dir, proj, rel)
        dst = os.path.join(root, proj, rel)
        if not os.path.isfile(src):
            print("  ❌ 备份缺失: %s" % src)
            continue
        shutil.copy2(src, dst)
        import hashlib
        got = hashlib.md5(open(dst, "rb").read()).hexdigest()
        print("  %s %-24s %s %s" % ("✅" if got == md5 else "❌", proj, rel[:40],
                                    "" if got == md5 else "md5 不符"))
        n += 1
    print("\n已还原 %d 个文件" % n)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="真的写文件（默认只检查）")
    ap.add_argument("--all", action="store_true", help="作用于全部派生工程")
    ap.add_argument("--restore", metavar="DIR", help="从备份目录整批还原")
    ap.add_argument("projs", nargs="*")
    a = ap.parse_args()

    if a.restore:
        return do_restore(a.restore)

    blocks = extract_blocks()
    root = os.path.dirname(SRC_ROOT)
    targets = DERIVED if a.all else a.projs
    if not targets:
        print("没有指定工程（用 --all 或列工程名）")
        return 2

    backup_dir = os.path.join(DEFAULT_BACKUP, time.strftime("%Y%m%d-%H%M%S"))
    bad = 0
    for p in targets:
        st, why = do_one(root, p, blocks, apply=a.apply, backup_dir=backup_dir)
        mark = {"written": "✅ 写入", "ready": "✅ 可写", "skip": "⏭  跳过",
                "fail": "❌ 失败", "missing": "❌ 缺失"}[st]
        print("%-24s %s%s" % (p, mark, ("  — " + why) if why else ""))
        if st in ("fail", "missing"):
            bad += 1
    if a.apply:
        print("\n备份目录: %s（还原用 --restore 该路径）" % backup_dir)
    print("%s：%d 个工程%s" % ("已写入" if a.apply else "可写",
                               len(targets) - bad, "" if not bad else "（%d 个有问题）" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
