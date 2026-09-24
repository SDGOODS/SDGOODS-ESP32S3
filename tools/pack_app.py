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
"""pack_app —— 校验并导出「不带地址」的应用包

**平台约定：交付给平台的应用包 = 纯应用镜像（app），不携带烧录地址。**
地址由设备上的分区表决定（见 docs/BUILD.md 第 4 节），包本身只回答"装的是什么"。
同一份 app.bin 因此能落到任何位置：旧设备写 `0x10000`，装了启动器的设备写某个空槽。

用法：

    python3 tools/pack_app.py                    # build/ → dist/SDGOODS_EBADGE_app.bin
    python3 tools/pack_app.py -b build_pub       # 指定构建目录
    python3 tools/pack_app.py -i some.bin        # 直接校验/导出某个文件
    python3 tools/pack_app.py --json             # 结构化输出（给 AI 助手、CI 用）
    python3 tools/pack_app.py --no-emit          # 只校验，不写 dist/

校验项（任一不过即拒绝）：

| # | 判据 | 为什么 |
|---|---|---|
| 1 | `0x8000` **不是**分区表魔数 `0xAA50` | 是则说明这是 `merged.bin`（合并镜像），带地址、从 `0x0` 起写 —— 会覆盖 bootloader 与分区表，设备变砖 |
| 2 | `0x0` 首字节 == `0xE9` | ESP 镜像魔数（bootloader 也是 `0xE9`，所以还要看第 3 项） |
| 3 | `0x20` == `0xABCD5432` | app 描述结构 `esp_app_desc_t`，只有应用镜像才有 |
| 4 | `0x0C` chip_id == `0x0009` | ESP32-S3（防止把别的芯片的固件传上来） |
| 5 | 体积 ≤ 槽上限（默认 2.9 MB，槽物理 3 MB 留余量） | 超了会写穿到相邻槽，把别的应用弄坏 |
| 6 | 应用身份不是模板默认名（`SDGOODS_EBADGE` 等） | 否则 app_id 撞车、数据目录互串（急着跳过加 `--allow-template-name`，第三方不该用） |

退出码：`0` 通过；`1` 校验失败；`2` 用法/文件错误。
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys

# --------------------------------------------------------------------------- 常量
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ESP 镜像格式（components/bootloader_support/include/esp_app_format.h）
APP_IMAGE_MAGIC = 0xE9            # esp_image_header_t.magic
APP_DESC_MAGIC = 0xABCD5432       # esp_app_desc_t.magic_word
PARTITION_TABLE_MAGIC = 0x50AA    # 分区表首条记录的 magic（ESP_PARTITION_MAGIC）
# ⚠ 端序别搞反：下面用 struct 小端读 uint16，得到的值是 0x50AA，**不是** 0xAA50。
#    分区表文件头两字节的字节序是 AA 50（实测 partition-table.bin 前 8 字节：
#    AA 50 01 02 00 90 00 00）。写成 0xAA50 会让「合并镜像」漏判 —— 踩过这个坑。
CHIP_ID_ESP32S3 = 0x0009
CHIP_NAMES = {
    0x0000: "ESP32",
    0x0002: "ESP32-S2",
    0x0005: "ESP32-C3",
    0x0009: "ESP32-S3",
}

OFF_CHIP_ID = 0x0C                                      # esp_image_header_t.chip_id
OFF_ENTRY_ADDR = 0x04                                   # esp_image_header_t.entry_addr
PT_OFFSET = 0x8000                                      # 分区表固定烧录地址
APP_DESC_OFFSET = 0x20                                  # sizeof(image_header)+sizeof(segment_header)
# project_name 相对 esp_app_desc_t 起点的偏移 48(0x30) ⇒ 绝对 0x50（实测核对过：
# SDGOODS_EBADGE.bin @0x50 = "SDGOODS_EBADGE"，HELLO_3.bin @0x50 = "HELLO_3"）。
OFF_PROJECT_NAME = 48
# 应用槽的**安全**上限。槽物理上是 3 MB，但必须留余量：
#   · app 分区不是百分百干净（末尾残留、对齐填充都算在 sizeBytes 里）
#   · 紧跟着就是下一个槽，写穿会把**别人的 app** 弄坏
# ⚠️ 这个值必须与平台侧一致（server/src/lib/firmwarePolicy.ts 的 SLOT_APP_MAX_BYTES = 2.9MB）。
#    两者漂移的表现很阴：作者**在本地校验通过**、上传过审上架，用户点刷机才被告知
#    装不进槽，而提交记录看起来全是绿的。
DEFAULT_SLOT_BYTES = int(2.9 * 1024 * 1024)

# 模板工程的**默认名** —— 开发者克隆后忘了改 project() 时，产出的就是这些。
# 为什么必须拦：app_id = esp_app_desc_t.project_name，而
#   · app 私有数据目录 = appdata/<app_id>/
#   · 卸载也按这个 app_id 删
# ⇒ 两个不同作者的 app 撞名 ⇒ 共用同一个数据目录、数据互串，卸载一个删掉另一个的数据。
# 旧 tools/new_app.py 只生成源码、**不会**提醒改工程名；new_app_project.py 已自动改名。
# 手动复制文件创建应用时仍可能漏改工程名 —— 漏改是默认结果。
TEMPLATE_DEFAULT_NAMES = {
    "SDGOODS_EBADGE",      # SDGOODS-ESP32S3 基础工程的默认值
    "SDGOODS_HELLO",       # SDGOODS-HELLO 示例工程的默认值
    "SDGOODS_LAUNCHER",    # 启动器（第三方 app 绝不该叫这个：会让平台误判成多应用宿主）
}

_SIZE_HEAD = max(PT_OFFSET + 2, APP_DESC_OFFSET + 256)


# --------------------------------------------------------------------------- 小工具
def _cstr(buf, off, n):
    """从定长字符数组里取出以 NUL 结尾的字符串。"""
    return buf[off:off + n].split(b"\x00")[0].decode("utf-8", "replace").strip()


def _human(n):
    return "%s B（%.2f MB）" % ("{:,}".format(n), n / 1024.0 / 1024.0)


def _project_name():
    """从顶层 CMakeLists.txt 的 project(...) 取工程名（决定 app 产物名）。"""
    try:
        with open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8") as f:
            m = re.search(r"^\s*project\(\s*([A-Za-z0-9_\-]+)", f.read(), re.M)
        if m:
            return m.group(1)
    except Exception:
        pass
    return "SDGOODS_EBADGE"


def _default_input(build_dir=None):
    """挑一个默认输入：优先 build/，其次按修改时间找最新的 build_*/。"""
    proj = _project_name() + ".bin"
    cands = []
    if build_dir:
        cands.append(os.path.join(build_dir, proj))
    else:
        cands.append(os.path.join(REPO, "build", proj))
        dirs = [d for d in _glob_build_dirs()]
        for d in dirs:
            cands.append(os.path.join(d, proj))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def _glob_build_dirs():
    try:
        names = os.listdir(REPO)
    except Exception:
        return []
    dirs = []
    for n in names:
        if n == "build" or n.startswith("build_"):
            p = os.path.join(REPO, n)
            if os.path.isdir(p) and os.path.isfile(os.path.join(p, "build.ninja")):
                dirs.append(p)
    dirs.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return dirs


# --------------------------------------------------------------------------- 核心校验
def verify_app_bin(path, max_bytes=DEFAULT_SLOT_BYTES, allow_template_name=False):
    """校验一个文件是否为「不带地址」的合法应用包。

    返回结果字典（不抛异常）：

        {ok, kind, errors[], warnings[], sizeBytes, sha256,
         chipId, chipName, entryAddr, segmentCount,
         projectName, version, buildDate, buildTime, idfVersion}

    allow_template_name=True 时放行 TEMPLATE_DEFAULT_NAMES 里的名字
    —— 官方自己发布那个 demo app 时用得着，第三方不应需要它。
    """
    res = {
        "path": os.path.abspath(path),
        "fileName": os.path.basename(path),
        "ok": False,
        "kind": "unknown",       # app | merged | not-app | unknown
        "errors": [],
        "warnings": [],
        "sizeBytes": 0,
        "maxBytes": max_bytes,
    }

    if not os.path.isfile(path):
        res["errors"].append("文件不存在：%s" % path)
        return res

    size = os.path.getsize(path)
    res["sizeBytes"] = size
    if size < _SIZE_HEAD:
        # 小文件做不了完整判定，但足以区分「选错文件」与「镜像被截断」
        first = -1
        try:
            with open(path, "rb") as f:
                b0 = f.read(1)
                first = b0[0] if b0 else -1
        except OSError:
            pass
        if first != APP_IMAGE_MAGIC:
            res["errors"].append(
                "文件只有 %s，首字节 0x%02X ≠ 0xE9：这不是 ESP 固件"
                "（分区表？截图？多半选错文件了）。" % (_human(size), first & 0xFF)
            )
        else:
            res["errors"].append(
                "文件只有 %s，像被截断的镜像（连 0x%X 处都没到），不可能是完整的应用包。"
                % (_human(size), PT_OFFSET)
            )
        return res

    # 头 + sha256 一起读（sha256 用于平台侧核对，全量算）
    with open(path, "rb") as f:
        head = f.read(_SIZE_HEAD)
        f.seek(0)
        h = hashlib.sha256()
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    res["sha256"] = h.hexdigest()

    app_magic = head[0]
    has_pt = struct.unpack_from("<H", head, PT_OFFSET)[0] == PARTITION_TABLE_MAGIC
    desc_magic = struct.unpack_from("<I", head, APP_DESC_OFFSET)[0]
    chip_id = struct.unpack_from("<H", head, OFF_CHIP_ID)[0]

    # ① 带地址的合并镜像：0x8000 处有分区表 —— 最先判，因为这是最危险的形态
    if has_pt:
        res["kind"] = "merged"
        res["errors"].append(
            "这是**合并镜像**（0x8000 处有分区表魔数）：它自带烧录地址、从 0x0 起写，"
            "会覆盖 bootloader 与分区表 —— 平台刷机后设备无法启动。"
        )
        res["hint"] = "请改传构建目录下的 %s.bin（纯应用镜像），而不是 merged.bin。" % _project_name()
        return res

    # ② 得是 ESP 镜像
    if app_magic != APP_IMAGE_MAGIC:
        res["errors"].append(
            "首字节 0x%02X ≠ 0xE9：不是 ESP 固件镜像（多半选错了文件）。" % app_magic
        )
        return res

    # ③ 得带 app 描述结构（这才是「应用镜像」）
    if desc_magic != APP_DESC_MAGIC:
        res["kind"] = "not-app"
        res["errors"].append(
            "0x%X 处没有 app 描述结构（读到 0x%08X，应为 0x%08X）：这不是应用镜像"
            "（bootloader？分区表？或者文件被截断）。" % (APP_DESC_OFFSET, desc_magic, APP_DESC_MAGIC)
        )
        return res

    # 通过形态判定，读出元数据
    d = APP_DESC_OFFSET
    res["kind"] = "app"
    res["chipId"] = chip_id
    res["chipName"] = CHIP_NAMES.get(chip_id, "未知芯片 (0x%04X)" % chip_id)
    res["entryAddr"] = struct.unpack_from("<I", head, OFF_ENTRY_ADDR)[0]
    res["segmentCount"] = head[1]
    res["projectName"] = _cstr(head, d + 48, 32)
    res["version"] = _cstr(head, d + 16, 32)
    res["buildTime"] = _cstr(head, d + 80, 16)
    res["buildDate"] = _cstr(head, d + 96, 16)
    res["idfVersion"] = _cstr(head, d + 112, 32)

    # ④ 芯片必须对得上
    if chip_id != CHIP_ID_ESP32S3:
        res["errors"].append(
            "芯片不符：镜像 chip_id = 0x%04X（%s），本设备是 ESP32-S3（0x0009）。"
            % (chip_id, CHIP_NAMES.get(chip_id, "未知"))
        )

    # ⑤ 体积不能超过槽上限
    if max_bytes and size > max_bytes:
        res["errors"].append(
            "体积超限：%s 超过槽上限 %s。超出的部分会写进相邻槽，把别的应用弄坏。"
            % (_human(size), _human(max_bytes))
        )
    elif max_bytes and size > max_bytes * 0.9:
        res["warnings"].append(
            "体积已达槽上限的 %.0f%%（%s / %s），再加素材就会超限。"
            % (100.0 * size / max_bytes, _human(size), _human(max_bytes))
        )

    if not res["projectName"]:
        res["warnings"].append("app 描述里的 project_name 为空，启动器列表里会没有名字。")
    elif res["projectName"] in TEMPLATE_DEFAULT_NAMES and not allow_template_name:
        res["errors"].append(
            "app 身份还是模板默认值「%s」—— 说明你克隆工程后没有改 CMakeLists.txt 里的 project()。"
            "这个名字是 app 的**唯一身份**：设备上 appdata/<app_id>/ 用它做数据目录、卸载也按它删。"
            "不改的话，你的 app 会和其它同样没改名的作者的 app 共用同一个数据目录（数据互串），"
            "而且用户卸载其中一个时，会把另一个的数据一起删掉。"
            "改法：编辑工程根 CMakeLists.txt 的 project(YOUR_APP_NAME) 后重新编译。"
            % res["projectName"]
        )
    if re.match(r"^[0-9a-f]{7,}(-dirty)?$", res.get("version") or ""):
        res["warnings"].append(
            "app 描述里的版本号是 git hash（%s），不是语义化版本 —— 因为工程没设 PROJECT_VER。"
            "想让设备里显示 1.0.0，在工程根放一个 version.txt（内容一行 1.0.0）后重新编译。"
            % res["version"]
        )

    res["ok"] = not res["errors"]
    return res


# --------------------------------------------------------------------------- 输出
def format_report(res, emitted=None, outdir=None):
    """把校验结果排成人类可读的多行文本（缩进交给调用方）。"""
    lines = []
    if res.get("errors"):
        lines.append("✘ 应用包校验未通过：%s" % res["fileName"])
        lines.append("")
        for e in res["errors"]:
            lines.append("  ✘ %s" % e)
        if res.get("hint"):
            lines.append("")
            lines.append("  → %s" % res["hint"])
        # 这段兜底说明**只属于形态问题**（误传了 merged 包）。撞名、超芯片之类的错误
        # 也跟着显示它会把人带偏 —— 看起来像是文件形态不对，实际是另一回事。
        if res.get("kind") != "app":
            lines.append("")
            lines.append("  说明：平台的应用包必须是**不带地址**的纯应用镜像 ——")
            lines.append("        地址由设备的分区表决定，包带着地址走就会写到 0x0，")
            lines.append("        覆盖 bootloader 与分区表，用户刷完无法启动。")
        return "\n".join(lines)

    used = ""
    if res.get("maxBytes"):
        used = "，槽上限 %s（用掉 %.0f%%）" % (
            _human(res["maxBytes"]), 100.0 * res["sizeBytes"] / res["maxBytes"])
    lines.append("✔ 应用包校验通过：%s" % res["fileName"])
    lines.append("")
    lines.append("  体积      %s%s" % (_human(res["sizeBytes"]), used))
    lines.append("  芯片      %s (0x%04X)" % (res.get("chipName"), res.get("chipId", 0)))
    lines.append("  项目名    %s" % (res.get("projectName") or "(空)"))
    lines.append("  版本      %s" % (res.get("version") or "(空)"))
    lines.append("  编译时间  %s %s" % (res.get("buildDate", ""), res.get("buildTime", "")))
    lines.append("  IDF       %s" % (res.get("idfVersion") or "(空)"))
    lines.append("  入口地址  0x%08X（虚拟地址，与烧录位置无关）" % res.get("entryAddr", 0))
    lines.append("  sha256    %s" % res.get("sha256", ""))
    if emitted:
        shown = os.path.relpath(emitted, REPO) if emitted.startswith(REPO + os.sep) else emitted
        lines.append("")
        lines.append("  已导出    %s" % shown)
    if res.get("warnings"):
        lines.append("")
        for w in res["warnings"]:
            lines.append("  ⚠ %s" % w)
    return "\n".join(lines)


# --------------------------------------------------------------------------- CLI
def build_parser():
    p = argparse.ArgumentParser(
        prog="pack_app",
        description="校验并导出「不带地址」的应用包（app.bin）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="示例：\n"
               "  python3 tools/pack_app.py                 # build/ → dist/\n"
               "  python3 tools/pack_app.py --json           # 给 AI / CI 用\n",
    )
    p.add_argument("-i", "--input", help="要校验的 bin（默认自动找 build*/<项目名>.bin）")
    p.add_argument("-b", "--build-dir", help="构建目录（如 build_pub）；默认自动挑选")
    p.add_argument("-o", "--outdir", default="dist", help="导出目录（默认 dist/，相对仓库根）")
    p.add_argument("--name", help="导出文件名（默认 <项目名>_app.bin）")
    p.add_argument("--max-size", type=int, default=DEFAULT_SLOT_BYTES,
                   help="槽上限字节数（默认 2.9MB，与平台侧 SLOT_APP_MAX_BYTES 一致；槽物理 3MB 需留余量）")
    p.add_argument("--no-emit", action="store_true", help="只校验，不导出")
    p.add_argument("--allow-template-name", action="store_true",
                   help="允许 app 身份仍是模板默认名（第三方不该用；官方发布 demo 时才需要）")
    p.add_argument("--json", action="store_true", help="输出 JSON（机器可读）")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)

    src = args.input or _default_input(args.build_dir)
    if not src:
        sys.stderr.write(
            "找不到应用包。请先编译：\n"
            "    idf.py build\n"
            "或用 --input 指定一个 .bin 文件。\n"
        )
        return 2
    if not os.path.isfile(src):
        sys.stderr.write("文件不存在：%s\n" % src)
        return 2

    res = verify_app_bin(src, max_bytes=args.max_size, allow_template_name=args.allow_template_name)

    emitted = None
    if res["ok"] and not args.no_emit:
        outdir = args.outdir if os.path.isabs(args.outdir) else os.path.join(REPO, args.outdir)
        name = args.name or (os.path.splitext(res["fileName"])[0] + "_app.bin")
        emitted = os.path.join(outdir, name)
        try:
            os.makedirs(outdir, exist_ok=True)
            with open(src, "rb") as fi, open(emitted, "wb") as fo:
                for chunk in iter(lambda: fi.read(1 << 20), b""):
                    fo.write(chunk)
            # 附带一份元数据，方便上传时直接取用
            meta_path = os.path.splitext(emitted)[0] + ".json"
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump({k: res.get(k) for k in (
                    "fileName", "sizeBytes", "sha256", "chipName", "chipId", "projectName",
                    "version", "buildDate", "buildTime", "idfVersion", "entryAddr")},
                    f, ensure_ascii=False, indent=2)
            res["emitted"] = emitted
            res["metaPath"] = meta_path
        except OSError as e:
            res["ok"] = False
            res["errors"].append("导出失败：%s" % e)

    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=2))
    else:
        print(format_report(res, emitted=emitted))
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
