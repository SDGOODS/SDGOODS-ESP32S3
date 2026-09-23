#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/new_standalone_project.py — 把本仓库一键派生为「独立应用工程」。

一步完成 G1 文档里最磨人的几件事：

  1. 复制仓库到新目录（排除 .git / build* / dist / managed_components 等）
  2. 改根 CMakeLists.txt 的 project(<名>) —— 同时决定
        · 产物名 build_pub/<名>.bin
        · flash 0x10050 的设备身份（控制中心 About 页 / 平台 Firmware.id 都读它）
  3. 把全仓写死的 'SDGOODS_EBADGE' 替换为新名
        （pack_app.py 的 TEMPLATE_DEFAULT_NAMES 拒绝集保持不变 ——
         派生工程仍应拒绝发布「没改名的官方模板」）
  4. 瘦应用注册表：s_apps[] 只保留一个 app（默认 flappy，或 --app 指定）
  5. 可选 --prune：删掉其它 demo 页面源码 + 同步 main/CMakeLists.txt 的 SRCS
  6. 可选 --boot-direct：开机直入保留的 app（改 main.c 首屏 + 注册表导航，
        并随 --prune 删主页）—— 这就是 SDGOODS-PLANE 当年的做法
  7. 在新目录 git init

用法:
  python3 tools/new_standalone_project.py <NEW_NAME> [选项]

  <NEW_NAME>   新工程名（同时是 CMake project() 名 / 产物名 / 设备身份）。
               只允许 [A-Za-z0-9_] 且长度 <= 32（esp_app_desc.project_name 上限）。
               例如 MY_PLANE / HELLO_WORLD / MY_BADGE

选项:
  --dest DIR        目标目录（默认: 源仓库同级的 ../<NEW_NAME>）
  --in-place       直接把当前仓库改造成独立工程（不清空 .git、不复制、不 git init）
  --app NAME       注册表里要保留的 app（如 flappy / plane）；默认第一个（当前是 flappy）
  --prune          删掉其它 demo 页面源码（ui_scan/rec/other page，开机直入时含 ui_home）
  --boot-direct    开机直入保留的 app（不再走主页启动台）
  --with-managed   复制时保留 managed_components（默认排除，首次编译由组件管理器重新下载）
  --no-commit      不自动 git commit（只 init + add）
  --dry-run        只打印将要做的改动，不落盘

注：本工具只做「改名 + 瘦注册表 + 删 demo + 替换硬编码」这类机械活；
    你真正的应用逻辑（画 UI / 接手势 / 接控制中心）请照 docs/STANDALONE_PROJECT.md 在
    生成的工程里用 app_template.c 当骨架写。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

OLD = "SDGOODS_EBADGE"

# --------------------------------------------------------------------------- 路径
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_ROOT = os.path.dirname(SCRIPT_DIR)  # tools/ 的上级 = 仓库根


def log(msg):
    print(msg)


def die(msg):
    print("✗ " + msg, file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------------------- 名称校验
def validate_name(name):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        die("工程名 %r 非法：只允许字母/数字/下划线，且不能以数字开头。" % name)
    if len(name) > 32:
        die("工程名 %r 超过 32 字符（esp_app_desc.project_name 上限），会被 IDF 拒绝。" % name)
    if name in ("SDGOODS_EBADGE", "SDGOODS_HELLO", "SDGOODS_LAUNCHER"):
        log("⚠ 警告：%r 是官方模板默认名，发布时会被 pack_app 拒绝。建议换个名字。" % name)


# --------------------------------------------------------------------------- 复制仓库
def copy_repo(src, dst, with_managed, dry):
    if os.path.exists(dst):
        die("目标目录已存在：%s\n（先删掉或换一个 --dest）" % dst)

    def ignore(d, names):
        ig = set()
        for n in names:
            if n == ".git":
                ig.add(n)
            elif n.startswith("build") and (n == "build" or n[5] == "_" or n[5:] == ""):
                ig.add(n)
            elif n in ("dist", "node_modules", "__pycache__", ".DS_Store", ".workbuddy"):
                ig.add(n)
            elif n == "managed_components" and not with_managed:
                ig.add(n)
        return ig

    if dry:
        log("[dry-run] 复制 %s -> %s (排除 .git/build*/dist/managed_components%s)"
            % (src, dst, "" if with_managed else " [保留]"))
        return
    log("→ 复制仓库 %s -> %s" % (src, dst))
    shutil.copytree(src, dst, ignore=ignore)


# --------------------------------------------------------------------------- 工具：读写
def read_text(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write_text(path, text, dry):
    if dry:
        return
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


# --------------------------------------------------------------------------- 1) project() 改名
def rename_project(root, new_name, dry):
    cmake = os.path.join(root, "CMakeLists.txt")
    text = read_text(cmake)
    new_text, n = re.subn(r"^(\s*project\()SDGOODS_EBADGE(\))", r"\1%s\2" % new_name, text, flags=re.M)
    if n == 0:
        # 已经是别的名字？尝试通用替换（保留原有括号/参数）
        new_text, n = re.subn(r"^(\s*project\()([A-Za-z0-9_]+)(\))", r"\1%s\2" % new_name, text, flags=re.M)
    if n == 0:
        die("未在根 CMakeLists.txt 找到 project(...) 行，无法改名。")
    log("  • 根 CMakeLists.txt: project() -> %s" % new_name)
    write_text(cmake, new_text, dry)


# --------------------------------------------------------------------------- 2) 全仓 token 替换（保护 pack_app 拒绝集）
def replace_token(root, new_name, dry):
    protected_block = None
    total = 0
    count_files = 0
    skip_dirs = {".git", "build", "dist", "node_modules", "__pycache__", "managed_components", ".workbuddy"}

    for dirpath, dirnames, filenames in os.walk(root):
        # 就地裁剪遍历，避免进大目录
        dirnames[:] = [d for d in dirnames if d not in skip_dirs
                       and not (d.startswith("build") and (d == "build" or d[5] in "_"))]
        for fn in filenames:
            if fn.endswith((".bin", ".elf", ".map", ".png", ".jpg", ".jpeg", ".gif",
                            ".pyc", ".o", ".a", ".pdf", ".zip")):
                continue
            p = os.path.join(dirpath, fn)
            try:
                text = read_text(p)
            except (UnicodeDecodeError, OSError):
                continue  # 二进制 / 不可读，跳过
            if OLD not in text:
                continue

            # pack_app.py 特例：保护 TEMPLATE_DEFAULT_NAMES 拒绝集
            if fn == "pack_app.py" and "TEMPLATE_DEFAULT_NAMES" in text:
                m = re.search(r"TEMPLATE_DEFAULT_NAMES = \{.*?\n\}", text, re.S)
                if m:
                    protected_block = m.group(0)
                    text = text[:m.start()] + "\x00PROT\x00" + text[m.end():]

            cnt = text.count(OLD)
            text = text.replace(OLD, new_name)

            if protected_block is not None:
                text = text.replace("\x00PROT\x00", protected_block)
                protected_block = None

            if cnt:
                total += cnt
                count_files += 1
                write_text(p, text, dry)

    log("  • 替换 %s -> %s：%d 处 / %d 个文件（pack_app 的 TEMPLATE_DEFAULT_NAMES 拒绝集已保留）"
        % (OLD, new_name, total, count_files))


# --------------------------------------------------------------------------- 解析注册表现有 app
APP_ENTRY_RE = re.compile(r"\{(.*?)\}", re.S)


def parse_apps(reg_text):
    m = re.search(r"static const sdgoods_app_t s_apps\[\] = \{(.*?)\n\};", reg_text, re.S)
    if not m:
        return []
    out = []
    for block in APP_ENTRY_RE.findall(m.group(1)):
        def g(pat):
            mm = re.search(pat, block)
            return mm.group(1) if mm else ""
        out.append({
            "zh": g(r'\.label_zh\s*=\s*"([^"]*)"'),
            "en": g(r'\.label_en\s*=\s*"([^"]*)"'),
            "icon": g(r'\.icon\s*=\s*"([^"]*)"'),
            "show": g(r'\.show\s*=\s*(\w+)'),
            "poll": g(r'\.poll\s*=\s*(\w+)'),
        })
    return out


def app_name_from(show_fn):
    # ui_flappy_start -> flappy
    m = re.match(r"ui_([A-Za-z0-9_]+)_start", show_fn)
    return m.group(1) if m else show_fn


# --------------------------------------------------------------------------- 3) 瘦注册表
def slim_registry(root, new_name, app_arg, prune, boot_direct, dry):
    reg_path = os.path.join(root, "main", "apps", "apps_registry.c")
    text = read_text(reg_path)
    apps = parse_apps(text)
    if not apps:
        die("解析 apps_registry.c 的 s_apps[] 失败。")

    kept = None
    if app_arg:
        for a in apps:
            if app_name_from(a["show"]) == app_arg or app_arg in a["show"]:
                kept = a
                break
        if kept is None:
            log("⚠ --app %s 不在现有注册表里，改用第一个：%s"
                % (app_arg, app_name_from(apps[0]["show"])))
    if kept is None:
        kept = apps[0]

    kname = app_name_from(kept["show"])
    kshow = kept["show"]
    kpoll = kept["poll"] or (kshow.replace("_start", "_poll"))
    kinclude = 'ui_%s.h' % kname

    log("  • 注册表只保留 app: %s (show=%s)" % (kname, kshow))

    # 重新生成 s_apps[] 块
    new_block = (
        "static const sdgoods_app_t s_apps[] = {\n"
        "    { .label_zh = \"%s\", .label_en = \"%s\", .icon = \"%s\",\n"
        "      .show = %s, .poll = %s },\n"
        "    /* 新应用插到这里（保持缩进即可） */\n"
        "};" % (kept["zh"] or kname, kept["en"] or kname, kept["icon"] or kname, kshow, kpoll)
    )
    text = re.sub(r"static const sdgoods_app_t s_apps\[\] = \{.*?\n\};", new_block, text, flags=re.S)

    # 删除「非保留 app」的 include（当前 s_apps 只有 flappy，所以默认不动；
    # 若未来多 app，这里会把其它 app 的 include 摘掉）
    for a in apps:
        if a is kept:
            continue
        inc = 'ui_%s.h' % app_name_from(a["show"])
        text = re.sub(r'\n#include "%s"' % re.escape(inc), "", text)

    # 始终确保保留 app 的 include 在（没有就加在 app_sdk 之后 / 文件头 include 区）
    if 'include "%s"' % kinclude not in text:
        text = text.replace(
            '#include "apps_registry.h"',
            '#include "apps_registry.h"\n#include "%s"' % kinclude, 1)

    # --- prune：删子页面 include + apps_poll 里的子页面 poll 调用 ---
    if prune:
        for page in ("ui_scan_page", "ui_rec_page", "ui_other_page"):
            text = re.sub(r'\n#include "%s.h"' % page, "", text)
            text = re.sub(r"\n\s*%s_poll\(\);" % page, "", text)
        # 删文件
        for page in ("ui_scan_page", "ui_rec_page", "ui_other_page"):
            for ext in (".c", ".h"):
                fp = os.path.join(root, "main", "apps", page + ext)
                if os.path.exists(fp):
                    if not dry:
                        os.remove(fp)
                    log("  • 删除 demo 文件: main/apps/%s%s" % (page, ext))
        # 若保留的不是 flappy，把 flappy 也删了（保持纯净单 app）
        if kname != "flappy":
            for ext in (".c", ".h"):
                fp = os.path.join(root, "main", "apps", "ui_flappy" + ext)
                if os.path.exists(fp):
                    if not dry:
                        os.remove(fp)
                    log("  • 删除非保留 app: main/apps/ui_flappy%s" % ext)

    # --- boot-direct：导航落点改到保留 app ---
    if boot_direct:
        text = text.replace("    ui_home_create();", "    %s();" % kshow)
        text = text.replace("    ui_home_show();", "    %s();" % kshow)
        if prune:
            # 连主页都删：去除 include 与文件
            text = re.sub(r'\n#include "ui_home.h"', "", text)
            for ext in (".c", ".h"):
                fp = os.path.join(root, "main", "apps", "ui_home" + ext)
                if os.path.exists(fp):
                    if not dry:
                        os.remove(fp)
                    log("  • 删除主页: main/apps/ui_home%s" % ext)

    write_text(reg_path, text, dry)
    return kname, kshow


# --------------------------------------------------------------------------- main.c 首屏
def patch_main_c(root, kshow, boot_direct, dry):
    if not boot_direct:
        return
    mp = os.path.join(root, "main", "main.c")
    text = read_text(mp)
    if "sdgoods_ui_home_create_show();" in text:
        text = text.replace("    sdgoods_ui_home_create_show();", "    %s();" % kshow, 1)
        write_text(mp, text, dry)
        log("  • main.c 首屏改为直入 %s()" % kshow)
    else:
        log("⚠ main.c 未找到 sdgoods_ui_home_create_show()，开机直入未套用（请手动改）。")


# --------------------------------------------------------------------------- 同步 CMake SRCS
def sync_cmake_srcs(root, prune, boot_direct, kname, dry):
    if not (prune or boot_direct):
        return
    cm = os.path.join(root, "main", "CMakeLists.txt")
    text = read_text(cm)
    drop = []
    if prune:
        drop += ["ui_scan_page.c", "ui_rec_page.c", "ui_other_page.c"]
        if kname != "flappy":
            drop.append("ui_flappy.c")
    if prune and boot_direct:
        drop.append("ui_home.c")
    if not drop:
        return
    for d in drop:
        text = re.sub(r'\n\s*"apps/%s"' % re.escape(d), "", text)
    write_text(cm, text, dry)
    log("  • main/CMakeLists.txt SRCS 移除: %s" % ", ".join(drop))


# --------------------------------------------------------------------------- git init
def git_init(dest, new_name, no_commit, dry):
    if dry:
        log("[dry-run] git init + add%s in %s" % ("" if no_commit else " + commit", dest))
        return
    subprocess.run(["git", "init"], cwd=dest, check=False)
    subprocess.run(["git", "add", "-A"], cwd=dest, check=False)
    if no_commit:
        return
    r = subprocess.run(["git", "commit", "-m",
                        "init: fork from SDGOODS-ESP32S3 as %s (standalone)" % new_name],
                       cwd=dest, capture_output=True, text=True)
    if r.returncode != 0:
        log("⚠ git commit 失败（可能没配 user.email/name），代码已 git add，请手动 commit。")


# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="把本仓库派生为独立应用工程")
    ap.add_argument("new_name", help="新工程名（CMake project() / 产物名 / 设备身份）")
    ap.add_argument("--dest", default=None, help="目标目录（默认 ../<new_name>）")
    ap.add_argument("--in-place", action="store_true", help="直接改造当前仓库（不复制）")
    ap.add_argument("--app", default=None, help="注册表里保留的 app（默认第一个）")
    ap.add_argument("--prune", action="store_true", help="删掉其它 demo 页面源码")
    ap.add_argument("--boot-direct", action="store_true", help="开机直入保留的 app")
    ap.add_argument("--with-managed", action="store_true", help="复制时保留 managed_components")
    ap.add_argument("--no-commit", action="store_true", help="不自动 git commit")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划，不落盘")
    args = ap.parse_args()

    validate_name(args.new_name)

    if args.in_place:
        dest = SRC_ROOT
        log("⚠ --in-place：将直接改造当前仓库 %s（不可撤销，建议先 git 提交）" % dest)
    else:
        dest = args.dest or os.path.join(os.path.dirname(SRC_ROOT), args.new_name)
        dest = os.path.abspath(dest)
        copy_repo(SRC_ROOT, dest, args.with_managed, args.dry_run)

    # dry-run 不真的复制，因此变换步骤改读 SRC_ROOT（写入是 no-op），仅做规划
    work_root = SRC_ROOT if (args.dry_run and not args.in_place) else dest

    log("")
    log("派生独立工程: %s" % args.new_name)
    log("  目标: %s" % dest)
    opts = []
    if args.prune:
        opts.append("prune")
    if args.boot_direct:
        opts.append("boot-direct")
    if args.with_managed:
        opts.append("with-managed")
    log("  选项: %s" % (", ".join(opts) or "(无)"))
    log("")

    log("[1/5] 改名 project() ...")
    rename_project(work_root, args.new_name, args.dry_run)

    log("[2/5] 替换硬编码 %s ..." % OLD)
    replace_token(work_root, args.new_name, args.dry_run)

    log("[3/5] 瘦注册表 ...")
    kname, kshow = slim_registry(work_root, args.new_name, args.app, args.prune, args.boot_direct, args.dry_run)

    log("[4/5] 首屏 / CMake 接线 ...")
    patch_main_c(work_root, kshow, args.boot_direct, args.dry_run)
    sync_cmake_srcs(work_root, args.prune, args.boot_direct, kname, args.dry_run)

    if not args.in_place:
        log("[5/5] git init ...")
        git_init(dest, args.new_name, args.no_commit, args.dry_run)
    else:
        log("[5/5] --in-place 跳过 git init")

    log("")
    if args.dry_run:
        log("✓ dry-run 完成，未做任何改动。去掉 --dry-run 真正执行。")
    else:
        log("✓ 完成。建议下一步：")
        log("    cd %s" % dest)
        log("    tools/build.sh            # 编译（注意产物是 build_pub/%s.bin）" % args.new_name)
        log("    tools/flash_local.sh      # 烧录（APP_BIN 已改为 %s.bin）" % args.new_name)
        log("    python3 tools/screenshot_recv.py -t   # 真机截图验证")
        log("  更多：docs/STANDALONE_PROJECT.md")


if __name__ == "__main__":
    main()
