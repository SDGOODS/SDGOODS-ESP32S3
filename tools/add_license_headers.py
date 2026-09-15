# SDGOODS 开放平台基础工程 · 开发工具
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
"""给自有源文件批量补齐 SDGOODS 许可头。

分级（与仓库的许可分层一一对应）：

  平台层  components/sdgoods_board/**（排除 fonts/）  -> Apache-2.0
  应用层  main/**（排除 patches/）                     -> PolyForm Noncommercial 1.0.0
  工具    tools/*.py                                  -> Apache-2.0

**刻意不动**这两处 —— 它们的许可由上游决定，我们无权更改：

  components/sdgoods_board/fonts/*.c   SIL OFL 1.1（Noto Sans SC 衍生，头部已有 OFL 声明）
  main/patches/**                       MIT（LVGL 衍生，见 main/patches/README.md）

用法：

    python3 tools/add_license_headers.py            # dry-run，只报告要改哪些文件
    python3 tools/add_license_headers.py --apply    # 落盘

幂等：已经含 SPDX 行的文件会跳过，可以反复跑。
新写完一个 .c/.h 之后跑一次，就不必手抄文件头。
"""
import glob
import os
import sys

# 仓库根 = 本脚本所在目录的上一级
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_URL = "https://github.com/SDGOODS/SDGOODS-ESP32S3"
COPYRIGHT = "Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)"
SPDX_MARK = "SPDX-License-Identifier"

APACHE_C = """/*
 * SDGOODS 开放平台基础工程 · 平台层（BSP）
 * %s
 *
 * %s
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于平台层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */
""" % (REPO_URL, COPYRIGHT)

NC_C = """/*
 * SDGOODS 开放平台基础工程 · 应用层示例
 * %s
 *
 * %s
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 *
 * Required Notice: %s
 *
 * 个人学习、研究与业余项目免费使用；商业用途需事前书面授权，见 LICENSING.md。
 * 分发时必须完整保留本声明 —— 这是许可条款，不是建议。
 */
""" % (REPO_URL, COPYRIGHT, COPYRIGHT)

APACHE_PY = """# SDGOODS 开放平台基础工程 · 开发工具
# %s
#
# %s
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
""" % (REPO_URL, COPYRIGHT)


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


def insert_header(path, header):
    """返回 (新内容, 状态)。已有 SPDX 行的文件返回 (None, ...)，保证幂等。"""
    with open(path, "r", encoding="utf-8", newline="") as f:
        src = f.read()

    if SPDX_MARK in src:
        return None, "skip-has-spdx"

    # Python 脚本若以 shebang 开头，许可头要插在它之后，否则 shebang 失效
    if src.startswith("#!"):
        nl = src.index("\n") + 1
        return src[:nl] + header + src[nl:], "insert-after-shebang"

    return header + "\n" + src, "insert"


def main():
    apply = "--apply" in sys.argv
    plat, app, tools = collect()

    groups = [("平台层 Apache-2.0", plat, APACHE_C),
              ("应用层 PolyForm NC", app, NC_C),
              ("工具 Apache-2.0", tools, APACHE_PY)]

    changed = skipped = 0
    for label, files, header in groups:
        print("\n=== %s（%d 个文件）===" % (label, len(files)))
        for p in files:
            rel = os.path.relpath(p, REPO)
            new, status = insert_header(p, header)
            if new is None:
                skipped += 1
                print("  跳过    %s" % rel)
                continue
            changed += 1
            if apply:
                with open(p, "w", encoding="utf-8", newline="") as f:
                    f.write(new)
                print("  写入    %s" % rel)
            else:
                print("  将加头  %s" % rel)

    # 提醒一下刻意不动的两处，避免有人以为脚本漏了
    print("\n未处理（许可由上游决定，不可更改）：")
    print("  components/sdgoods_board/fonts/*.c   SIL OFL 1.1")
    print("  main/patches/**                      MIT")
    print("\n---- 汇总 ----")
    print("%s：%d   跳过（已有声明）：%d" % ("已写入" if apply else "待处理", changed, skipped))


if __name__ == "__main__":
    main()
