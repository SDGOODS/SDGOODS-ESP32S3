# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程 · 开发工具
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
# 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
# 以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
"""给自有源文件批量补齐 / 刷新 SDGOODS 许可头。

分级（与仓库的许可分层一一对应）：

  平台层  components/sdgoods_board/**（排除 fonts/）  -> Apache-2.0
  应用层  main/**（排除 patches/）                     -> Apache-2.0
  工具    tools/*.py                                  -> Apache-2.0

本仓库（平台层与应用层）统一以 Apache-2.0 发布，可自由商用、可闭源分发。

**刻意不动**这两处 —— 它们的许可由上游决定，我们无权更改：

  components/sdgoods_board/fonts/*.c   SIL OFL 1.1（Noto Sans SC 衍生，头部已有 OFL 声明）
  main/patches/**                       MIT（LVGL 衍生，见 main/patches/README.md）

用法：

    python3 tools/add_license_headers.py            # dry-run，只报告要改哪些文件
    python3 tools/add_license_headers.py --apply    # 落盘

幂等且可「重新盖章」：脚本认识自己以前写过的头（靠 `SDGOODS 开放平台基础工程`
这个标记定位），所以改了上面的模板再跑一次，全部文件的旧头会被**整体替换**成新头，
而不是被跳过。新写完一个 .c/.h 之后跑一次，就不必手抄文件头。

⚠️ 模板改动会影响仓库里所有自有源文件（几十个），改完请 `git diff --stat` 扫一眼。
"""
import glob
import os
import sys

# 仓库根 = 本脚本所在目录的上一级
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_URL = "https://github.com/SDGOODS/SDGOODS-ESP32S3"
COPYRIGHT = "Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)"
# 归属声明要折成两行，且续行必须带上注释前缀（C 用 " * "，Python 用 "# "），
# 否则续行会掉进注释块外面 —— 曾经踩过这个坑，别改回单串拼接。
_OWNER_L1 = "「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，"
_OWNER_L2 = "以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。"
OWNERSHIP_C = "%s\n *   %s" % (_OWNER_L1, _OWNER_L2)
OWNERSHIP_PY = "%s\n#   %s" % (_OWNER_L1, _OWNER_L2)
MARKER = "SDGOODS 开放平台基础工程"      # 定位「本脚本写过的头」用的标记

APACHE_C = """/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 平台层（板级支持包 BSP）
 * %s
 *
 * %s
 * %s
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */
""" % (REPO_URL, COPYRIGHT, OWNERSHIP_C)

NC_C = """/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * %s
 *
 * %s
 * %s
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */
""" % (REPO_URL, COPYRIGHT, OWNERSHIP_C)

APACHE_PY = """# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程 · 开发工具
# %s
#
# %s
# %s
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
""" % (REPO_URL, COPYRIGHT, OWNERSHIP_PY)


def collect():
    plat = []
    for ext in ("*.c", "*.h"):
        plat += glob.glob(os.path.join(REPO, "components/sdgoods_board/**", ext), recursive=True)
    plat = [p for p in plat if "/fonts/" not in p]

    app = []
    for ext in ("*.c", "*.h"):
        app += glob.glob(os.path.join(REPO, "main/**", ext), recursive=True)
    app = [p for p in app if "/patches/" not in p]

    tools = [p for p in glob.glob(os.path.join(REPO, "tools", "*.py"))
             if os.path.abspath(p) != os.path.abspath(__file__)]

    return sorted(set(plat)), sorted(set(app)), sorted(set(tools))


def _c_block_end(src):
    """src 以 /* 开头且注释块内含标记 -> 返回块结束下标（'*/' 之后）。"""
    if not src.startswith("/*"):
        return None
    end = src.find("*/")
    if end == -1 or MARKER not in src[:end]:
        return None
    return end + 2


def _py_block(src):
    """返回 (块起始, 块结束)。允许 shebang 在前；块是开头连续的一段 # 注释行。"""
    start = 0
    if src.startswith("#!"):
        nl = src.find("\n")
        if nl == -1:
            return None
        start = nl + 1
    if not src[start:start + 200].lstrip().startswith("#"):
        return None
    i, end = start, None
    while i < len(src):
        nl = src.find("\n", i)
        nl = len(src) if nl == -1 else nl
        if not src[i:nl].lstrip().startswith("#"):
            break
        end = nl
        i = nl + 1
    if end is None or MARKER not in src[start:end]:
        return None
    return start, end


def stamp(path, header):
    """返回 (新内容 或 None, 状态)。"""
    with open(path, "r", encoding="utf-8", newline="") as f:
        src = f.read()

    body = _c_block_end(src)
    if body is None:
        py = _py_block(src)
        body = py[1] if py else None

    # 没有受管头 -> 首次插入。Python 若以 shebang 开头，头要插在它之后。
    if body is None:
        if src.startswith("#!"):
            nl = src.index("\n") + 1
            return src[:nl] + header + "\n" + src[nl:], "insert-after-shebang"
        return header + "\n" + src, "insert"

    # 已有受管头 -> 比较后决定是否替换（实现「改模板后重新盖章」）
    head_start = _py_block(src)[0] if _c_block_end(src) is None else 0
    old_head = src[head_start:body]
    new_head = header.rstrip("\n")
    if old_head == new_head:
        return None, "up-to-date"

    after = body
    if after < len(src) and src[after] == "\n":
        after += 1     # 连同紧跟的一个换行一起吃掉，避免多出空行
    return src[:head_start] + header + src[after:], "restamp"


def main():
    apply = "--apply" in sys.argv
    plat, app, tools = collect()

    groups = [("平台层 Apache-2.0", plat, APACHE_C),
              ("应用层 Apache-2.0", app, NC_C),
              ("工具 Apache-2.0", tools, APACHE_PY)]

    changed = skipped = 0
    for label, files, header in groups:
        print("\n=== %s（%d 个文件）===" % (label, len(files)))
        for p in files:
            rel = os.path.relpath(p, REPO)
            new, status = stamp(p, header)
            if new is None:
                skipped += 1
                continue
            changed += 1
            if apply:
                with open(p, "w", encoding="utf-8", newline="") as f:
                    f.write(new)
                print("  %-22s %s" % (status, rel))
            else:
                print("  %-22s %s" % (status, rel))

    # 提醒一下刻意不动的两处，避免有人以为脚本漏了
    print("\n未处理（许可由上游决定，不可更改）：")
    print("  components/sdgoods_board/fonts/*.c   SIL OFL 1.1")
    print("  main/patches/**                      MIT")
    print("\n---- 汇总 ----")
    print("%s：%d   已是当前模板（跳过）：%d" % ("已写入" if apply else "待处理", changed, skipped))


if __name__ == "__main__":
    main()
