#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""触摸「写盘静默窗口」判据（源码形状层）

背景：写 SPI flash 期间整片关 cache + 关中断，触摸 I2C 的 ISR 代码在 flash 里
⇒ 事务拖过超时 ⇒ 进 IDF 的 i2c_master_clear_bus()（不 yield 的 50 ms 忙等）
⇒ 连续几趟关中断够 300 ms ⇒ `Interrupt wdt timeout on CPU0` + rst:0xc ⇒ 设备重启。
真机抓到过两次（2026-10-07 / 10-31），addr2line 都落在 cst816_read。

本脚本钉的就是「防止它再回来」的几处**形状**，不是行为 —— 行为靠真机日志
（`touch: flash write window -> touch paused` + 零 `slow read` / 零 Guru）。

用法：
    python3 tools/verify_touch_flash_window.py            # 基线：必须全绿
    python3 tools/verify_touch_flash_window.py --selftest # 阴控：每条判据都要能被打红

🔴 纪律：
  · 每条判据都有对应变异（--selftest），**没打红过的判据等于没写**。
  · 量不到就抛错，不 print 了事（"没找到" ≠ "通过了"）。
  · 不按"前 N 个字符"截断，全部用函数体 / 锚点定位。
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P_INPUT_C = os.path.join(ROOT, "components/sdgoods_board/src/sdgoods_input.c")
P_INPUT_H = os.path.join(ROOT, "components/sdgoods_board/include/sdgoods_input.h")
P_APPDATA = os.path.join(ROOT, "main/app_data_store.c")


class Fail(Exception):
    pass


def read(p):
    if not os.path.exists(p):
        raise Fail("文件不存在: %s" % p)
    with open(p, encoding="utf-8") as f:
        return f.read()


def func_body(src, name):
    """取函数体（配平花括号）。找不到抛 Fail —— 绝不静默返回空串。"""
    m = re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name), src)
    if not m:
        raise Fail("找不到函数定义: %s" % name)
    i = src.index("{", m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1:j]
    raise Fail("函数体花括号不平衡: %s" % name)


def at(src, needle, what=""):
    """在 src 里定位 needle，返回偏移；找不到抛 Fail。"""
    i = src.find(needle)
    if i < 0:
        raise Fail("找不到锚点%s: %r" % ("（%s）" % what if what else "", needle))
    return i


def brace_block(src, i_from):
    """从 i_from 起，取其后第一个 `{ ... }` 的 (开括号, 闭括号) 偏移。配平失败抛 Fail。"""
    i_open = src.index("{", i_from)
    depth = 0
    for j in range(i_open, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return i_open, j
    raise Fail("花括号不平衡（起点偏移 %d 附近）" % i_from)


def once(src, needle):
    """断言 needle 恰好出现一次，返回偏移。"""
    n = src.count(needle)
    if n != 1:
        raise Fail("锚点 %r 出现 %d 次，期望 1 次" % (needle, n))
    return src.find(needle)


def define_of(src, macro):
    """取 `#define MACRO 值` 的整数值；不是纯数字抛 Fail。"""
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(macro), src, re.M)
    if not m:
        raise Fail("找不到宏定义: %s" % macro)
    v = m.group(1)
    if not re.fullmatch(r"\d+", v):
        raise Fail("%s 的值不是整数字面量: %r" % (macro, v))
    return int(v)


# ============================================================================
# 判据
# ============================================================================
def checks():
    C = read(P_INPUT_C)
    H = read(P_INPUT_H)
    A = read(P_APPDATA)
    out = []

    def ck(tag, fn):
        try:
            fn()
            out.append((True, tag, ""))
        except Fail as e:
            out.append((False, tag, str(e)))
        except Exception as e:  # 判据自己写错 = 假红，也要暴露出来
            out.append((False, tag, "判据异常: %r" % e))

    # ---- ① 常量 -----------------------------------------------------------
    def c1():
        v = define_of(C, "CST816_RD_TIMEOUT_MS")
        if v != 90:
            raise Fail("CST816_RD_TIMEOUT_MS = %d，期望 90（写盘期间实测最坏 78.7 ms）" % v)
        at(C, "pdMS_TO_TICKS(CST816_RD_TIMEOUT_MS)", "cst816_read 的超时入参")
    ck("①-1 读超时 = 90 ms 且真的传给 i2c_master_write_read_device", c1)

    def c2():
        body = func_body(C, "cst816_read")
        if "pdMS_TO_TICKS(50)" in body:
            raise Fail("cst816_read 里还有写死的 pdMS_TO_TICKS(50)")
        if "CST816_RD_TIMEOUT_MS" not in body:
            raise Fail("cst816_read 没用 CST816_RD_TIMEOUT_MS")
    ck("①-2 cst816_read 不含写死的 50 ms", c2)

    def c3():
        h = define_of(H, "SDGOODS_TOUCH_FLASH_WIN_MAX_MS")
        if h != 3000:
            raise Fail("SDGOODS_TOUCH_FLASH_WIN_MAX_MS = %d，期望 3000" % h)
        body = func_body(C, "sdgoods_touch_flash_window")
        # 🔴 钉**整句**，不是"含这个宏名" —— 只查名字的话，把条件改成 `if (0)` 照样绿
        #    （宏名还留在下面的赋值里），等于判据是装饰。
        at(body, "if (ms > SDGOODS_TOUCH_FLASH_WIN_MAX_MS) {", "上限比较")
        at(body, "ms = SDGOODS_TOUCH_FLASH_WIN_MAX_MS;", "钳制赋值")
        at(body, "if (ms <= 0) {", "下界钳制（ms<=0 会让截止时刻落在过去）")
    ck("①-3 窗口上限 3000 ms 且比较与钳制都在（整句）", c3)

    def c4():
        v = define_of(C, "SDGOODS_TOUCH_FLASH_GRACE_MS")
        if v != 40:
            raise Fail("GRACE = %d，期望 40" % v)
        body = func_body(C, "sdgoods_touch_flash_window_end")
        if "SDGOODS_TOUCH_FLASH_GRACE_MS" not in body:
            raise Fail("_end 里没用 GRACE（fclose 后的回程没盖住）")
    ck("①-4 收尾 grace = 40 ms 且 _end 真的用它", c4)

    # ---- ② 落点（最关键：静默判断必须在 I2C 之前）----------------------------
    def c5():
        body = func_body(C, "touchpad_read")
        i_guard = at(body, "touch_quiet_busy()", "静默判断")
        i_i2c = at(body, "cst816_read(", "第一次 I2C 读")
        if not (i_guard < i_i2c):
            raise Fail("静默判断在 I2C 之后（guard=%d, i2c=%d）—— 事务已经发出去了，照样崩"
                       % (i_guard, i_i2c))
    ck("②-1 touchpad_read：静默判断在第一次 I2C 事务之前", c5)

    def c6():
        body = func_body(C, "touchpad_read")
        i_guard = at(body, "if (touch_quiet_busy())", "静默块")
        # 配平这个 if 的花括号，取出静默块体
        i_open = body.index("{", i_guard)
        depth = 0
        i_close = -1
        for j in range(i_open, len(body)):
            if body[j] == "{":
                depth += 1
            elif body[j] == "}":
                depth -= 1
                if depth == 0:
                    i_close = j
                    break
        if i_close < 0:
            raise Fail("静默块花括号不平衡")
        blk = body[i_open + 1:i_close]
        for bad in ("cst816_read", "i2c_master", "i2c_"):
            if bad in blk:
                raise Fail("静默块里出现了 I2C 调用 %r —— 窗口内必须一个事务都不发" % bad)
        if blk.count("return") != 2:
            raise Fail("静默块里 return 有 %d 个，期望 2 个（写盘补松手 / 退避沿用上次状态）"
                       % blk.count("return"))
    ck("②-2 静默块内零 I2C 调用、两条分支都 return", c6)

    def c7():
        body = func_body(C, "touchpad_read")
        i_guard = at(body, "if (touch_quiet_busy())", "静默块")
        i_open, i_close = brace_block(body, i_guard)
        blk = body[i_open + 1:i_close]

        # 写盘分支：必须先补一次 RELEASED，否则窗口一开用户的手指从 UI 视角消失
        i_rel_if = at(blk, "if (s_quiet_release)", "写盘补松手分支")
        r_open, r_close = brace_block(blk, i_rel_if)
        rel_blk = blk[r_open + 1:r_close]
        if "LV_INDEV_STATE_RELEASED" not in rel_blk:
            raise Fail("写盘分支没补 RELEASED —— 窗口期间 UI 会卡在按下态")
        if "return" not in rel_blk:
            raise Fail("写盘分支没有 return —— 会掉下去发 I2C 事务")
        if "s_touch_state = LV_INDEV_STATE_RELEASED" not in rel_blk:
            raise Fail("写盘分支没同步轮询缓存 s_touch_state —— 轮询型 app 会以为还按着")

        # 退避分支：**只许沿用上次状态**。这里补 RELEASED 会把"正按着"误判成"松手"，
        # 长按 / 游戏按住会被切断 —— 所以这条是"必须不出现"，不是"最好别出现"。
        tail = blk[r_close + 1:]
        if "s_touch_state" not in tail:
            raise Fail("退避分支没沿用上次状态（s_touch_state）")
        if "LV_INDEV_STATE_RELEASED" in tail:
            raise Fail("退避分支里补了 RELEASED —— 长按/游戏按住会被切断")
        if "return" not in tail:
            raise Fail("退避分支没有 return —— 会掉下去发 I2C 事务")
        if "s_backoff_pauses++" not in tail:
            raise Fail("退避分支没有计数（真机就看不到'退避真的发生了'）")
    ck("②-3 写盘分支补松手、退避分支沿用上次状态（不误判松手）", c7)

    # ---- ③ 语义（只延不缩 / 松手标记只置 true）-------------------------------
    def c8():
        body = func_body(C, "touch_quiet_push")
        at(body, "(until - s_quiet_until_ms) > 0", "只延不缩的守卫")
        n = body.count("s_quiet_until_ms = until;")
        if n != 1:
            raise Fail("s_quiet_until_ms = until 出现 %d 次，期望 1 次（且必须在守卫块内）" % n)
        i_assign = body.index("s_quiet_until_ms = until;")
        i_guard = body.index("(until - s_quiet_until_ms) > 0")
        if not (i_guard < i_assign):
            raise Fail("赋值不在守卫之后 —— 后到的更早截止会把前一家的改早")
    ck("③-1 截止时刻只延不缩（唯一赋值点带守卫）", c8)

    def c9():
        n_true = C.count("s_quiet_release = true;")
        n_false = C.count("s_quiet_release = false;")
        if n_true != 1:
            raise Fail("s_quiet_release = true 出现 %d 次，期望 1 次（在 touch_quiet_push 里）"
                       % n_true)
        if n_false != 1:
            raise Fail("s_quiet_release = false 出现 %d 次，期望 1 次（touchpad_read 消费点）。"
                       "多于 1 次说明有人会抹掉写盘窗口那次松手" % n_false)
        body = func_body(C, "touch_quiet_push")
        if "s_quiet_release = true;" not in body:
            raise Fail("置 true 的地方不在 touch_quiet_push —— 慢读退避可能抹掉松手标记")
    ck("③-2 松手标记只置 true、只被消费一次", c9)

    def c10():
        body = func_body(C, "sdgoods_touch_flash_window_end")
        # 钉整句：grace 和"不补松手"都要在**这一行**上，不是"函数里出现过 false"
        at(body, "touch_quiet_push(touch_now_ms() + SDGOODS_TOUCH_FLASH_GRACE_MS, false);",
           "_end 的收尾 push（grace + 不补松手）")
        body_b = func_body(C, "sdgoods_touch_flash_window")
        at(body_b, "touch_quiet_push(touch_now_ms() + ms, true);", "开窗的 push（请求补松手）")
    ck("③-3 window 请求补松手、_end 不补（整句）", c10)

    # ---- ④ 慢读自适应退避 --------------------------------------------------
    def c11():
        if define_of(C, "TOUCH_SLOW_US") != 20000:
            raise Fail("TOUCH_SLOW_US 不是 20000（正常一次读 ~200 µs，阈值低了会把抖动算成慢）")
        at(C, "cost_us >= TOUCH_SLOW_US", "慢读判定")
    ck("④-1 慢读阈值 20 ms 且真的参与判定", c11)

    def c12():
        body = func_body(C, "touchpad_read")
        i_thr = at(body, "if (cost_us >= TOUCH_SLOW_US)", "慢读分支")
        i_zero = at(body, "s_slow_run = 0", "清零")
        if not (i_thr < i_zero):
            raise Fail("s_slow_run = 0 应在慢读分支的 else 里（一次正常立刻清零）")
        # 清零必须在 else 分支：检查 else 出现在两者之间
        between = body[i_thr:i_zero]
        if "else" not in between:
            raise Fail("清零不在 else 分支 —— 退避会把自己锁死在慢档")
        at(body, "if (backoff > TOUCH_SLOW_MAX_MS) {", "退避上限比较（整句）")
    ck("④-2 一次正常立刻清零 + 退避有上限（整句）", c12)

    def c13():
        body = func_body(C, "touchpad_read")
        i_thr = at(body, "if (cost_us >= TOUCH_SLOW_US)", "慢读分支")
        i_push = at(body[i_thr:], "touch_quiet_push", "退避真的推了截止时刻") + i_thr
        seg = body[i_push:body.index(";", i_push)]
        if "false" not in seg:
            raise Fail("慢读退避不该请求补松手（%r）" % seg)
    ck("④-3 慢读退避只推迟、不补松手", c13)

    # ---- ⑤ 出声（真机验收的正面证据）---------------------------------------
    def c14():
        at(C, "flash write window -> touch paused", "写盘让路日志")
        at(C, "slow read: %d us", "慢读日志")
    ck("⑤-1 让路与慢读都必须打日志（真机验收靠这两句）", c14)

    # ---- ⑥ 写盘方接入（参照实现就是最容易被照抄的地方）----------------------
    def c15():
        at(A, '#include "sdgoods_input.h"', "include")
        n_b = A.count("sdgoods_touch_flash_window(")
        n_e = A.count("sdgoods_touch_flash_window_end()")
        if n_b != 1:
            raise Fail("app_data_store 里 _window( 出现 %d 次，期望 1 次" % n_b)
        if n_e != 2:
            raise Fail("_end() 出现 %d 次，期望 2 次（成功路径 + fopen 失败路径）" % n_e)
    ck("⑥-1 app_data_store：window 1 次、end 2 次（失败路径也收）", c15)

    def c16():
        i_win = at(A, "sdgoods_touch_flash_window(300)", "开窗")
        i_open = at(A, 'fopen(path, "w")', "写文件")
        if not (i_win < i_open):
            raise Fail("开窗在 fopen 之后 —— 真正写字的时候窗口还没开")
        i_close = at(A, "fclose(f);", "fclose")
        i_end_after = A.find("sdgoods_touch_flash_window_end()", i_close)
        if i_end_after < 0:
            raise Fail("fclose 之后没有 _end()：FAT 目录项更新那一段没人盖")
    ck("⑥-2 开窗在 fopen 之前、end 在 fclose 之后", c16)

    def c17():
        # 失败路径：fopen 返回 NULL 的分支里也要有 end
        i_null = at(A, "if (!f) {", "fopen 失败分支")
        i_close = A.index("}", i_null)
        if "sdgoods_touch_flash_window_end()" not in A[i_null:i_close]:
            raise Fail("fopen 失败分支没有 _end() —— 配对永不成对是最容易漏的一条")
    ck("⑥-3 fopen 失败分支也关了窗", c17)

    return out


# ============================================================================
# 阴控：每条判据都要能被"写坏"打红
# ----------------------------------------------------------------------------
# 元素 = (判据编号, 变异说明, 文件, 原文, 改坏后)
# 🔴 用**编号**匹配（不是整条标题）：判据标题改字是常事，标题一改就静默"空枪"，
#    比不测更糟。改编号 = 判据换了内容，那时才应该逼着人回来改这张表。
#    "编号不存在"单独报错（见 run_selftest 的前置校验），不让改名悄悄溜过。
# ============================================================================
MUTATIONS = [
    ("①-1", "超时不再走宏", P_INPUT_C,
     "pdMS_TO_TICKS(CST816_RD_TIMEOUT_MS)", "pdMS_TO_TICKS(90)"),
    ("①-3", "上限条件被短路", P_INPUT_C,
     "if (ms > SDGOODS_TOUCH_FLASH_WIN_MAX_MS) {", "if (0) {"),
    ("①-4", "收尾丢了 grace", P_INPUT_C,
     "touch_quiet_push(touch_now_ms() + SDGOODS_TOUCH_FLASH_GRACE_MS, false);",
     "touch_quiet_push(touch_now_ms(), false);"),
    # ②-1 / ②-2 是同一处：静默判断被短路 ⇒ ②-1 必红
    ("②-1", "静默判断被短路（等于没有）", P_INPUT_C,
     "if (touch_quiet_busy()) {", "if (false) {"),
    ("②-3", "退避分支改成补松手（会切断长按）", P_INPUT_C,
     "data->state = s_touch_state;\n        data->point = s_touch_point;",
     "data->state = LV_INDEV_STATE_RELEASED;\n        data->point = s_touch_point;"),
    ("③-1", "只延不缩的守卫被短路", P_INPUT_C,
     "if ((until - s_quiet_until_ms) > 0) {", "if (true) {"),
    ("③-2", "松手标记可以被置 false", P_INPUT_C,
     "s_quiet_release = true;", "s_quiet_release = false;"),
    ("③-3", "_end 也补一次松手", P_INPUT_C,
     "touch_quiet_push(touch_now_ms() + SDGOODS_TOUCH_FLASH_GRACE_MS, false);",
     "touch_quiet_push(touch_now_ms() + SDGOODS_TOUCH_FLASH_GRACE_MS, true);"),
    ("④-2", "慢读后不清零（退避锁死在慢档）", P_INPUT_C,
     "s_slow_run = 0;", "s_slow_run = 1;"),
    ("④-2", "退避上限条件被短路", P_INPUT_C,
     "if (backoff > TOUCH_SLOW_MAX_MS) {", "if (0) {"),
    ("⑤-1", "让路日志被抹掉（真机就没有正面证据）", P_INPUT_C,
     '"flash write window -> touch paused (x%u)"', '"paused (x%u)"'),
    ("⑥-1", "失败路径漏了 end", P_APPDATA,
     "sdgoods_touch_flash_window_end();   /* 失败路径也要收：配对永不成对是最容易漏的一条 */",
     ""),
    ("⑥-2", "开窗被删（写盘时窗口还没开）", P_APPDATA,
     "sdgoods_touch_flash_window(300);", ""),
]


def run_selftest():
    base = checks()
    red = [t for ok, t, _ in base if not ok]
    print("=" * 78)
    print("阴控开始：基线 %d 条全绿=%s，变异 %d 组" % (len(base), not red, len(MUTATIONS)))
    if red:
        for t in red:
            print("  ❌ 基线不绿，阴控无意义: %s" % t)
        return 2

    # 前置校验：每个变异指向的判据编号必须真的存在（判据被删/改号时不许静默通过）
    ids = set()
    for _, tag, _ in base:
        ids.add(tag.split(" ")[0])
    missing = sorted({m[0] for m in MUTATIONS} - ids)
    if missing:
        print("  ❌ 变异表指向不存在的判据编号: %s（判据被删或改了号，请同步这张表）"
              % ", ".join(missing))
        return 2

    saved = {}
    ok_all = True
    try:
        for i, (cid, desc, path, old, new) in enumerate(MUTATIONS, 1):
            src = read(path)
            if src.count(old) != 1:
                print("  ❌ 变异 %d/%d [%s] %s —— 锚点出现 %d 次（期望 1）: %r"
                      % (i, len(MUTATIONS), cid, desc, src.count(old), old[:60]))
                ok_all = False
                continue
            saved.setdefault(path, src)
            with open(path, "w", encoding="utf-8") as f:
                f.write(src.replace(old, new, 1))
            res = checks()
            got = {t.split(" ")[0] for okk, t, _ in res if not okk}
            if cid in got:
                print("  ✅ 变异 %d/%d [%s] %s" % (i, len(MUTATIONS), cid, desc))
            else:
                print("  ❌ 变异 %d/%d [%s] **空枪**（判据没红）: %s" % (i, len(MUTATIONS), cid, desc))
                ok_all = False
            with open(path, "w", encoding="utf-8") as f:
                f.write(saved[path])
    finally:
        for path, src in saved.items():
            with open(path, "w", encoding="utf-8") as f:
                f.write(src)
        # 还原后必须复核：判据仍全绿，否则"还原"本身没落盘
        back = checks()
        bad = [t for okk, t, _ in back if not okk]
        if bad:
            print("  ❌ 还原后基线不绿（源码可能没还原成功）:")
            for t in bad:
                print("     - %s" % t)
            ok_all = False
    print("=" * 78)
    print("阴控结果：%s" % ("全部命中 ✅" if ok_all else "有漏 ❌"))
    return 0 if ok_all else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true", help="跑阴控：逐条把源码写坏，要求判据打红")
    args = ap.parse_args()

    if args.selftest:
        return run_selftest()

    res = checks()
    bad = 0
    for ok, tag, msg in res:
        if ok:
            print("  ✅ %s" % tag)
        else:
            bad += 1
            print("  ❌ %s\n       %s" % (tag, msg))
    print("-" * 78)
    print("%d/%d 通过" % (len(res) - bad, len(res)))
    if bad:
        return 1
    print("提示：形状绿只代表防线在代码里；行为绿要真机日志 ——")
    print("      `touch: flash write window -> touch paused` 出现、且零 `slow read` / 零 Guru / 零 rst。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
