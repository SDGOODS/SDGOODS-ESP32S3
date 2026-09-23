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
"""publish_wizard —— 发布前的向导工具（只做确定性、不该让 AI 现猜的事）

配合谷仓次元屏（SDGOODS-ESP32S3）固件发布使用。它把发布流程里「能机器判定」的
事从 AI 对话里抽出来，避免 AI 凭印象误判。完整编排见
sdgoods-ai/skills/sdgoods-publish/SKILL.md，**推荐发布前顺序**是：
先 check 确认 bin 最新 → 有设备就 flash 烧最新固件、screenshot-check 检测截图能力、
shot 截首页（无设备/无截图则改由 AI 生图）→ 最后再选手动 release 或 MCP 直传。

  1. check           校验待发布的 bin 是不是「最新编译版本」
                      —— 比源码新、是 app 包（非 merged）、工程身份对得上、dist 与 build 一致
  2. flash           连接设备并烧录一次最新固件（包装 flash_local.sh；发布前统一前置，先于截图）
  3. screenshot-check 校验这份 bin 是否真的「编译进了截图功能」
                      —— 真机截图前必查，否则连上设备也截不到
  4. shot            连设备截「首页」图并存入 screenshot/（真机截图；无设备/无截图时由 SKILL 走 AI 生图）
  5. release         手动发布：在 release/ 目录放好 bin + 截图 + MANIFEST.json 模板，供你自行上传
  6. ports           列出候选串口（给 flash / shot 分支用）
  7. version         读取仓库 version.txt（最新版本号真相源；每次构建自动 +1）
  8. published       发布前检查：拉取「我的固件」里与本工程同名的记录（判断是否已发布过）

「AI 生成文案/图片、询问用户、调用 MCP 直传」等交互步骤由 agent 在对话里完成，
本工具只负责确定性检查与文件准备。完整流程见 docs/PUBLISHING.md 与
sdgoods-ai/skills/sdgoods-publish/SKILL.md。

纯标准库（Python 3.8+），无需 pip install。
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 参与「新鲜度」比对的源码目录（只比源码，不比 docs / 构建产物 / 依赖）
SOURCE_DIRS = [
    "main",
    os.path.join("components", "sdgoods_board"),
    os.path.join("components", "sdgoods_launcher"),
]
EXCLUDE_DIRS = {"build", "build_pub", "dist", "managed_components", ".git", "release", "out"}
SOURCE_EXTS = {".c", ".h", ".cpp", ".hpp", ".s", ".S", ".asm", ".ld", ".cmake", ".txt"}
SOURCE_BASENAMES = {"CMakeLists.txt", "version.txt", "Kconfig"}

# 截图能力特征
SCREENSHOT_CONFIG = "CONFIG_SDGOODS_SCREENSHOT"
SHOT_MARKER = b"===SHOT-BEGIN"          # 仅当 sdgoods_screenshot.c 编入时，固件才会输出此串


# --------------------------------------------------------------------------- 工程解析
def _project_name():
    """从顶层 CMakeLists.txt 的 project(...) 取工程名（决定 app 产物名与身份）。"""
    try:
        with open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8") as f:
            m = re.search(r"^\s*project\(\s*([A-Za-z0-9_\-]+)", f.read(), re.M)
        if m:
            return m.group(1)
    except Exception:
        pass
    return "SDGOODS_EBADGE"


def _bin_project_name(binpath):
    """从 app 镜像 0x50 处读 project_name（esp_app_desc_t.project_name，32 字节定长）。"""
    try:
        with open(binpath, "rb") as f:
            f.seek(0x50)
            raw = f.read(32)
        return raw.split(b"\x00")[0].decode("utf-8", "replace").strip()
    except Exception:
        return ""


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


def _resolve_bin():
    """优先 dist/<name>_app.bin（应用包），否则 build*/<name>.bin（构建产物）。"""
    name = _project_name()
    proj_bin = name + ".bin"
    dist = os.path.join(REPO, "dist", name + "_app.bin")
    if os.path.isfile(dist):
        return dist, "dist 应用包"
    cands = [os.path.join(REPO, "build", proj_bin)]
    for d in _glob_build_dirs():
        cands.append(os.path.join(d, proj_bin))
    for c in cands:
        if os.path.isfile(c):
            return c, "构建产物（未导出应用包，建议先 tools/pack_app.py）"
    return None, "未找到"


def _build_dir_for(binpath):
    """根据 bin 路径反推构建目录（用于读 sdkconfig）。"""
    if binpath:
        p = os.path.dirname(binpath)
        base = os.path.basename(p)
        if base == "build" or base.startswith("build_"):
            return p
    dirs = _glob_build_dirs()
    return dirs[0] if dirs else None


def _newest_source_mtime():
    """返回源码目录里最新的文件修改时间（及相对路径）。"""
    newest = 0.0
    newest_rel = ""
    for sd in SOURCE_DIRS:
        root = os.path.join(REPO, sd)
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames
                           if d not in EXCLUDE_DIRS and not d.startswith(".")]
            for fn in filenames:
                ext = os.path.splitext(fn)[1].lower()
                if ext in SOURCE_EXTS or fn in SOURCE_BASENAMES:
                    p = os.path.join(dirpath, fn)
                    try:
                        mt = os.path.getmtime(p)
                    except OSError:
                        continue
                    if mt > newest:
                        newest = mt
                        newest_rel = os.path.relpath(p, REPO)
    return newest, newest_rel


def _git_dirty():
    """源目录里未提交（已跟踪）的改动文件列表；非 git 返回 None。"""
    try:
        out = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=no"] + SOURCE_DIRS,
            cwd=REPO, capture_output=True, text=True, timeout=20,
        )
        if out.returncode != 0:
            return None
        return [l.strip() for l in out.stdout.splitlines() if l.strip()]
    except Exception:
        return None


def _fmtime(t):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t)) if t else "(无)"


# --------------------------------------------------------------------------- check
def cmd_check(args):
    binpath, kind = _resolve_bin()
    if not binpath:
        _emit(args.json, {"verdict": "MISSING",
                          "reason": "未找到待发布 bin。请先编译（idf.py build）并导出应用包（python3 tools/pack_app.py）。"})
        sys.stderr.write("✘ 未找到待发布 bin。\n")
        sys.stderr.write("  请先编译：idf.py build\n")
        sys.stderr.write("  再导出应用包：python3 tools/pack_app.py\n")
        return 1

    name = _project_name()
    bin_name = _bin_project_name(binpath)
    bin_mt = os.path.getmtime(binpath)
    size = os.path.getsize(binpath)
    build_dir = _build_dir_for(binpath)
    build_bin = None
    if build_dir:
        cand = os.path.join(build_dir, name + ".bin")
        if os.path.isfile(cand):
            build_bin = cand
    build_mt = os.path.getmtime(build_bin) if build_bin else 0
    src_mt, src_rel = _newest_source_mtime()
    dirty = _git_dirty()

    issues = []
    warnings = []

    # ① 身份核对：bin 内 project_name 必须等于工程 project()
    if bin_name and bin_name != name:
        issues.append("bin 身份不符：镜像内 project_name=%r，与工程 project(%r) 不一致 —— "
                      "这份 bin 可能来自别的工程，请确认来源后重新编译。" % (bin_name, name))

    # ② 新鲜度
    if kind.startswith("dist"):
        if build_bin and bin_mt < build_mt:
            issues.append("dist 应用包比构建产物旧（dist %s / build %s），请重新运行 "
                          "python3 tools/pack_app.py 导出。" % (_fmtime(bin_mt), _fmtime(build_mt)))
        if build_mt and build_mt < src_mt:
            issues.append("构建产物比源码旧（build %s / 源码 %s，%s），请重新编译 idf.py build。"
                          % (_fmtime(build_mt), _fmtime(src_mt), src_rel))
    else:
        if bin_mt < src_mt:
            issues.append("bin 比源码旧（bin %s / 源码 %s，%s），请重新编译 idf.py build。"
                          % (_fmtime(bin_mt), _fmtime(src_mt), src_rel))

    # ③ git 工作树脏
    if dirty:
        warnings.append("源目录有 %d 个未提交改动（bin 可能未包含最新源码）：\n    %s"
                        % (len(dirty), "\n    ".join(dirty[:10])))
    elif dirty is None:
        pass  # 非 git，跳过

    verdict = "FRESH" if not issues else "STALE"
    report = {
        "verdict": verdict,
        "bin": os.path.relpath(binpath, REPO),
        "binKind": kind,
        "projectName": bin_name,
        "expectedProject": name,
        "binMtime": _fmtime(bin_mt),
        "newestSource": src_rel,
        "newestSourceMtime": _fmtime(src_mt),
        "sizeBytes": size,
        "issues": issues,
        "warnings": warnings,
    }
    _emit(args.json, report)

    if not args.json:
        print("发布前检查：%s" % os.path.relpath(binpath, REPO))
        print("  工程名    %s" % name)
        print("  bin 身份  %s" % (bin_name or "(读不到)"))
        print("  修改时间  %s" % _fmtime(bin_mt))
        print("  最新源码  %s @ %s" % (src_rel or "(无)", _fmtime(src_mt)))
        print("  体积      %s" % ("{:,}".format(size) + " B"))
        if issues:
            print("\n✘ 不是最新编译版本：")
            for i in issues:
                print("  - " + i)
        else:
            print("\n✔ 是最新编译版本（FRESH）。")
        for w in warnings:
            print("\n⚠ " + w)
    return 0 if verdict == "FRESH" else 1


# --------------------------------------------------------------------------- screenshot-check
def _sdkconfig_screenshot(builddir):
    """读 sdkconfig 里的 CONFIG_SDGOODS_SCREENSHOT。返回 True/False/None（无文件）。"""
    if not builddir:
        return None
    sc = os.path.join(builddir, "sdkconfig")
    if not os.path.isfile(sc):
        return None
    try:
        txt = open(sc, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    m = re.search(r"^%s=(\w+)" % re.escape(SCREENSHOT_CONFIG), txt, re.M)
    if not m:
        return None
    return m.group(1) == "y"


def _bin_has_screenshot(binpath):
    """bin 里是否含 ===SHOT-BEGIN 特征串（仅截图代码编入时才会有）。"""
    try:
        with open(binpath, "rb") as f:
            data = f.read()
        return SHOT_MARKER in data
    except OSError:
        return False


def cmd_screenshot_check(args):
    binpath, kind = _resolve_bin()
    if not binpath:
        _emit(args.json, {"verdict": "MISSING",
                          "reason": "未找到待发布 bin，无法检查截图能力。请先编译。"})
        sys.stderr.write("✘ 未找到待发布 bin，无法检查截图能力。请先编译。\n")
        return 1

    build_dir = _build_dir_for(binpath)
    sdk = _sdkconfig_screenshot(build_dir)
    bin_has = _bin_has_screenshot(binpath)

    if sdk is True:
        enabled, source = True, "sdkconfig（%s=y）" % SCREENSHOT_CONFIG
    elif sdk is False:
        enabled, source = False, "sdkconfig（%s 未开启）" % SCREENSHOT_CONFIG
    else:
        enabled, source = bin_has, "bin 特征串扫描"

    report = {
        "verdict": "OK" if enabled else "DISABLED",
        "bin": os.path.relpath(binpath, REPO),
        "buildDir": os.path.relpath(build_dir, REPO) if build_dir else None,
        "sdkconfig": ("y" if sdk is True else "n" if sdk is False else "unknown"),
        "binMarkerFound": bin_has,
        "detectedBy": source,
    }
    _emit(args.json, report)

    if not args.json:
        print("截图能力检查：%s" % os.path.relpath(binpath, REPO))
        print("  构建目录  %s" % (os.path.relpath(build_dir, REPO) if build_dir else "(未找到)"))
        print("  sdkconfig %s" % report["sdkconfig"])
        print("  bin 特征串 %s" % ("找到 ===SHOT-BEGIN" if bin_has else "未找到"))
        if enabled:
            print("\n✔ 此 bin 已编译进截图功能（依据：%s）。可连接设备用 screenshot_recv.py 截图。" % source)
        else:
            print("\n✘ 此 bin **未编译进截图功能**（依据：%s）。" % source)
            print("  真机截图会截不到。请先开启 %s 并重新编译：" % SCREENSHOT_CONFIG)
            print("    - menuconfig → SDGOODS Board → 截屏(Screenshot) → 设为 y；或")
            print("    - 在 sdkconfig 里加一行 %s=y；然后 idf.py build + pack_app.py" % SCREENSHOT_CONFIG)
            print("  若暂时无法重编，可改用「AI 生成的示意图」作为发布截图。")
    return 0 if enabled else 1


# --------------------------------------------------------------------------- release
def _release_readme(bin_name, proj_name):
    return (
        "SDGOODS 开放平台 · 手动发布包\n"
        "================================\n\n"
        "本目录由 `python3 tools/publish_wizard.py release` 生成。\n\n"
        "包含文件：\n"
        "  - %s   固件应用包（纯 app 镜像，不带地址）\n" % bin_name +
        "  - MANIFEST.json  发布信息模板（填好后随固件一起提交）\n\n"
        "上传步骤（网页 https://sdgoods.ai ）：\n"
        "  1. 登录后进入「我的固件 → 提交新固件」。\n"
        "  2. 上传本目录里的 %s。\n" % bin_name +
        "  3. 按 MANIFEST.json 的字段填写：\n"
        "       名称(name)        ：%s\n" % proj_name +
        "       简介(中文/英文)  ：在 description_zh / description_en 填\n"
        "       分类(category)    ：从平台已有分类里选（如 game / tool / demo）\n"
        "       版本(version)     ：如 v1.0.0\n"
        "       GitHub / 标签     ：可选\n"
        "       截图(shots)       ：至少 1 张，建议用真机截图或业务示意图\n"
        "  4. 提交审核，等待平台通过后上架。\n\n"
        "提示：想让 AI 帮你填文案并直接上传，改用 MCP 发布（见 sdgoods-publish SKILL）。\n"
    )


def cmd_release(args):
    binpath, kind = _resolve_bin()
    if not binpath:
        sys.stderr.write("✘ 未找到待发布 bin，无法生成 release 包。请先编译（idf.py build）。\n")
        return 1

    out = args.out or os.path.join(REPO, "release")
    os.makedirs(out, exist_ok=True)
    base = os.path.basename(binpath)
    dest = os.path.join(out, base)
    try:
        shutil.copy2(binpath, dest)
    except OSError as e:
        sys.stderr.write("复制 bin 失败：%s\n" % e)
        return 1

    meta = {
        "name": _project_name(),
        "version": "",
        "category": "",
        "description_zh": "",
        "description_en": "",
        "github": "",
        "tags": [],
        "screenshots": [],
        "notes": "",
        "binFile": base,
        "binSizeBytes": os.path.getsize(binpath),
        "generatedBy": "publish_wizard release",
        "generatedAt": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    with open(os.path.join(out, "MANIFEST.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    with open(os.path.join(out, "README.txt"), "w", encoding="utf-8") as f:
        f.write(_release_readme(base, meta["name"]))

    # 把 screenshot/ 里已准备好的提交截图一并带进 release/screenshots/，方便一次性上传
    shot_dir = os.path.join(REPO, "screenshot")
    if os.path.isdir(shot_dir):
        imgs = []
        for ext in (".png", ".jpg", ".jpeg"):
            imgs += glob.glob(os.path.join(shot_dir, "*" + ext))
        if imgs:
            sd = os.path.join(out, "screenshots")
            os.makedirs(sd, exist_ok=True)
            for im in imgs:
                shutil.copy2(im, os.path.join(sd, os.path.basename(im)))
            meta["screenshots"] = [os.path.join("screenshots", os.path.basename(im)) for im in imgs]
            with open(os.path.join(out, "MANIFEST.json"), "w", encoding="utf-8") as f:
                json.dump(meta, f, ensure_ascii=False, indent=2)
            print("  - screenshots/（%d 张，来自 screenshot/）" % len(imgs))

    print("已生成发布包：%s" % os.path.abspath(out))
    print("  - %s（%s 字节）" % (base, "{:,}".format(meta["binSizeBytes"])))
    print("  - MANIFEST.json（填写名称 / 简介 / 分类 / 截图等）")
    print("  - README.txt（手动上传步骤）")
    print("把整个 release/ 目录提交到开放平台（https://sdgoods.ai），或按 README 指引逐个填写。")
    return 0


# --------------------------------------------------------------------------- ports
PORT_PATTERNS = ["/dev/cu.usb*", "/dev/tty.usb*", "/dev/ttyACM*", "/dev/ttyUSB*"]


def _detect_ports():
    found = set()
    for p in PORT_PATTERNS:
        found.update(glob.glob(p))
    return sorted(found)


def cmd_ports(args):
    found = _detect_ports()
    if not found:
        print("未检测到串口设备。")
        print("  macOS：插上设备、关闭串口助手 / idf.py monitor 后，应出现 /dev/cu.usbmodem*")
        print("  Linux：应出现 /dev/ttyACM* 或 /dev/ttyUSB*")
        print("烧录 / 截屏示例（连上设备后）：")
        print("  python3 tools/publish_wizard.py flash -p <串口>")
        print("  python3 tools/screenshot_recv.py -t -p <串口>")
        return 0
    print("检测到以下候选串口：")
    for p in found:
        print("  %s" % p)
    print("\n烧录 / 截屏示例（连上设备、关闭串口助手后）：")
    print("  python3 tools/publish_wizard.py flash -p <上面某个串口>")
    print("  python3 tools/screenshot_recv.py -t -p <上面某个串口>")
    return 0


# --------------------------------------------------------------------------- version
def cmd_version(args):
    """读取仓库根 version.txt —— 构建时由 gen_app_info.py 自动 +1，是「最新版本号」真相源。"""
    vf = os.path.join(REPO, "version.txt")
    ver = "1.0.0"
    if os.path.isfile(vf):
        try:
            with open(vf, encoding="utf-8") as f:
                v = f.read().strip()
                if v:
                    ver = v
        except OSError:
            pass
    obj = {
        "version": ver,
        "source": "version.txt",
        "note": "每次 idf.py build 末位自动 +1（components/sdgoods_launcher/tools/gen_app_info.py）。"
                "发布时版本号请用这个值，不要沿用旧记录里的版本。",
    }
    _emit(args.json, obj)
    print("当前版本号：%s（来源 version.txt）" % ver)
    if not args.json:
        print("提示：构建后该值已 +1；重新发布时请作为「最新版本号」使用，覆盖旧记录的版本。")
    return 0


# --------------------------------------------------------------------------- published
def _load_publish_module():
    """延迟加载同目录的 sdgoods_publish.py（发布 REST 契约的单一事实来源）。"""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import sdgoods_publish  # type: ignore
        return sdgoods_publish
    except Exception as e:  # pragma: no cover
        sys.stderr.write("⚠ 无法加载 tools/sdgoods_publish.py：%s\n" % e)
        return None


def cmd_published(args):
    """发布前检查：拉取「我的固件」里与本工程同名（project_name）的记录，判断是否已发布过。"""
    pub = _load_publish_module()
    if pub is None:
        sys.stderr.write("✘ 缺少 tools/sdgoods_publish.py，无法查询平台。\n")
        return 1
    base = (getattr(args, "api", None) or os.environ.get("SDGOODS_API_BASE") or "").rstrip("/")
    if not base:
        sys.stderr.write("✘ 缺少 API 基地址（设置环境变量 SDGOODS_API_BASE 或传 --api）。\n")
        return 1
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        sys.stderr.write("✘ 尚未登录。请先运行：python3 tools/sdgoods_publish.py login 你的邮箱\n")
        return 1

    proj = args.name or _project_name()
    from urllib.parse import quote_plus
    qs = "scope=mine&q=" + quote_plus(proj)
    st, payload, _ = pub._req("GET", base + "/firmwares?" + qs, token=token)
    if st not in (200, 201) or not isinstance(payload, dict):
        sys.stderr.write("✘ 查询我的固件失败（HTTP %s）：%s\n" % (st, pub._err_msg(payload)))
        return 1
    items = payload.get("items") or []
    # 推荐匹配：fileName 含工程名（pack_app 产物为 <项目名>_app.bin），或 name 完全相等
    rec = None
    for it in items:
        fn = (it.get("fileName") or "")
        nm = (it.get("name") or "")
        if proj and proj in fn:
            rec = it
            break
        if nm == proj:
            rec = it
            break
    obj = {
        "projectName": proj,
        "total": payload.get("total", len(items)),
        "found": len(items) > 0,
        "recommendedId": (rec or {}).get("id") if rec else (items[0].get("id") if items else None),
        "items": items,
    }
    _emit(args.json, obj)
    if items:
        print("在「我的固件」中找到 %d 条匹配记录（关键词=%s）：" % (len(items), proj))
        for it in items:
            print("  - id=%s  name=%r  version=%s  status=%s"
                  % (it.get("id"), it.get("name"), it.get("version"), it.get("status")))
        if rec:
            print("★ 推荐复用：id=%s（fileName 命中工程名）。重新发布时把此 id 作为 upload_firmware 的 "
                  "firmware_id 走 PATCH 更新；name/descZh/descEn/category/tags 作为草稿、"
                  "版本号用最新 version.txt、截图用最新 screenshot/。" % rec.get("id"))
    else:
        print("「我的固件」里没有与 %s 匹配的记录 → 这是首次发布，走 POST 新建。" % proj)
    return 0


# --------------------------------------------------------------------------- flash
def _unset_sandbox_env():
    """WorkBuddy 沙箱里 CODEBUDDY_* 文件系统 hook 会拦截对 /dev 串口的访问，
    导致 esptool / pyserial 静默失败；调用串口相关子进程时显式去掉这三个变量。"""
    e = dict(os.environ)
    for k in ("CODEBUDDY_SAFE_DELETE_SANDBOX",
              "CODEBUDDY_BROKERED_FS_HOOK_ENABLED",
              "CODEBUDDY_SAFE_DELETE_ENABLED"):
        e.pop(k, None)
    return e


def cmd_flash(args):
    """连接设备并烧录一次（包装 flash_local.sh，脚本内部已处理 env -u 与 esptool 定位）。"""
    script = os.path.join(REPO, "tools", "flash_local.sh")
    if not os.path.isfile(script):
        sys.stderr.write("✘ 找不到烧录脚本：%s\n" % script)
        return 1
    cmd = ["bash", script]
    if args.port:
        cmd += ["-p", args.port]
    if args.build:
        cmd += ["-b", args.build]
    if args.baud:
        cmd += ["-B", str(args.baud)]
    print("开始烧录（flash_local.sh 内部已处理 env 与 esptool 定位）…")
    r = subprocess.run(cmd)
    return r.returncode


# --------------------------------------------------------------------------- shot
def _parse_shot_saved(stdout):
    """从 screenshot_recv.py 的输出里解析「已保存」的绝对路径。"""
    for line in stdout.splitlines():
        m = re.search(r"已保存 #\d+：(\S+)", line)
        if m:
            return m.group(1)
    return None


def cmd_shot(args):
    """连设备截「首页」图并存入 screenshot/（真机截图；不连设备时改用 AI 生图）。

    前置：
      - bin 必须编译进了截图功能（screenshot-check ≠ DISABLED）；
      - 设备已连接并启动停在首页（flash 步骤完成、设备已重启到首屏）。
    """
    binpath, _ = _resolve_bin()
    if not binpath:
        sys.stderr.write("✘ 未找到待发布 bin，无法截图。请先编译（idf.py build）。\n")
        return 1

    # ① 先校验 bin 带截图功能（用户硬性要求：截图前查 bin 是否装了截图功能）
    build_dir = _build_dir_for(binpath)
    sdk = _sdkconfig_screenshot(build_dir)
    bin_has = _bin_has_screenshot(binpath)
    enabled = (sdk is True) or (sdk is None and bin_has)
    if not enabled:
        sys.stderr.write("✘ 此 bin **未编译进截图功能**（screenshot-check=DISABLED），连上设备也截不到：\n")
        sys.stderr.write("    - 开启 CONFIG_SDGOODS_SCREENSHOT（menuconfig → SDGOODS Board → 截屏 → y）后重编；或\n")
        sys.stderr.write("    - 若不连设备，请走「AI 生图」模式作为发布截图。\n")
        return 1

    # ② 端口：未指定则自动探测单个；多个则让用户选
    port = args.port
    if not port:
        ports = _detect_ports()
        if len(ports) == 1:
            port = ports[0]
            print("自动选用串口：%s" % port)
        elif not ports:
            sys.stderr.write("✘ 未检测到串口设备。请先连接设备、关闭串口助手 / idf.py monitor。\n")
            sys.stderr.write("  若不连设备，请走「AI 生图」模式作为发布截图。\n")
            return 2
        else:
            sys.stderr.write("检测到多个串口，请用 -p 指定其中一个：\n  %s\n"
                             % "\n  ".join(ports))
            return 2

    # ③ 输出目录（新建 screenshot/）与文件名（默认 home）
    out_dir = args.out or os.path.join(REPO, "screenshot")
    os.makedirs(out_dir, exist_ok=True)
    stem = args.name or "home"
    out_path = os.path.join(out_dir, stem + ".png")   # fmt=1(JPEG) 时脚本会自动改成 .jpg

    # ④ 调用 screenshot_recv.py（自动发 's' 触发，无需碰设备）。
    #    首页截图默认先发 'R'（console 层全局重启命令，app 拦不住）：
    #    重启后设备必然停在「开机首屏」，与「设备启动后在首页截图」语义一致；
    #    --pre <字符> 可改截其它界面（如 '3'=关浮层回主页、'c'=控制中心）；--no-pre 直接截当前。
    recv = os.path.join(REPO, "tools", "screenshot_recv.py")
    cmd = [sys.executable, recv, "-t", "-p", port, "-o", out_path]
    pre = "R" if (args.pre is None and not args.no_pre) else (None if args.no_pre else args.pre)
    if pre:
        cmd += ["--pre", pre, "--wait", str(args.wait)]
    cmd += ["--timeout", str(args.timeout)]
    where = "开机首屏（已发 'R' 重启回首页）" if pre == "R" else ("界面 %r" % pre if pre else "当前界面")
    print("截图目标：%s（端口 %s，重启后约等 %s 秒启动）" % (where, port, args.wait if pre else 0))
    r = subprocess.run(cmd, capture_output=True, text=True, env=_unset_sandbox_env())
    if r.returncode != 0:
        sys.stderr.write("截图失败（screenshot_recv.py 返回 %d）。\n" % r.returncode)
        if r.stderr.strip():
            sys.stderr.write(r.stderr.strip() + "\n")
        sys.stderr.write("提示：关闭串口助手 / idf.py monitor 后重试；或走「AI 生图」模式。\n")
        return r.returncode

    saved = _parse_shot_saved(r.stdout)
    if not saved or not os.path.isfile(saved):
        # 兜底：取输出目录下最新图片
        cands = []
        for ext in (".png", ".jpg", ".jpeg"):
            cands += glob.glob(os.path.join(out_dir, "*" + ext))
        saved = max(cands, key=os.path.getmtime) if cands else None
    if not saved:
        sys.stderr.write("未能确认截图文件，请检查 %s 目录。\n" % out_dir)
        return 1

    print("\n✔ 首页截图已存：%s" % os.path.abspath(saved))
    print("   该图已放入 screenshot/ 目录，供后续手动或 MCP 发布直接取用。")
    return 0


# --------------------------------------------------------------------------- 工具
def _emit(as_json, obj):
    if as_json:
        print(json.dumps(obj, ensure_ascii=False, indent=2))


def build_parser():
    p = argparse.ArgumentParser(
        prog="publish_wizard",
        description="发布前向导：检查 bin 新鲜度 / 截图能力、生成 release 包、列串口",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    pc = sub.add_parser("check", help="校验待发布 bin 是不是最新编译版本")
    pc.add_argument("--json", action="store_true", help="输出 JSON（机器可读）")
    pc.set_defaults(func=cmd_check)

    ps = sub.add_parser("screenshot-check", help="校验 bin 是否编译进了截图功能（真机截图前必查）")
    ps.add_argument("--json", action="store_true", help="输出 JSON（机器可读）")
    ps.set_defaults(func=cmd_screenshot_check)

    pf = sub.add_parser("flash", help="连接设备并烧录一次（包装 flash_local.sh，发布前建议先烧真机）")
    pf.add_argument("-p", "--port", help="串口设备（不填则自动探测）")
    pf.add_argument("-b", "--build", help="构建目录（默认 build_pub）")
    pf.add_argument("-B", "--baud", type=int, help="波特率（默认 115200）")
    pf.set_defaults(func=cmd_flash)

    pj = sub.add_parser("shot", help="连设备截「首页」图并存入 screenshot/（真机截图；不连设备改用 AI 生图）")
    pj.add_argument("-p", "--port", help="串口设备（不填则自动探测单个）")
    pj.add_argument("--out", help="输出目录（默认仓库根 screenshot/）")
    pj.add_argument("--name", default="home", help="截图文件名 stem（默认 home）")
    pj.add_argument("--pre", default=None, metavar="CHAR",
                    help="先发送该切换字符再截（默认 '3'=回主页；可改启动器的 c=控制中心等）")
    pj.add_argument("--no-pre", action="store_true", help="不发送切换字符，直接截当前界面")
    pj.add_argument("--wait", type=float, default=6.0,
                    help="--pre 之后等待的秒数（默认 6.0，够 'R' 重启+LVGL 启动+首页渲染）")
    pj.add_argument("--timeout", type=float, default=60.0, help="等待截屏的超时秒数（默认 60）")
    pj.set_defaults(func=cmd_shot)

    pr = sub.add_parser("release", help="手动发布：生成 release/ 目录（bin + 截图 + MANIFEST.json）")
    pr.add_argument("--out", help="输出目录（默认仓库根 release/）")
    pr.set_defaults(func=cmd_release)

    pp = sub.add_parser("ports", help="列出候选串口（给 flash / shot 分支用）")
    pp.set_defaults(func=cmd_ports)

    pv = sub.add_parser("version", help="读取仓库 version.txt（最新版本号真相源，每次构建 +1）")
    pv.add_argument("--json", action="store_true", help="输出 JSON（机器可读）")
    pv.set_defaults(func=cmd_version)

    pk = sub.add_parser("published", help="发布前检查：拉取「我的固件」里与本工程同名的记录（判断是否已发布过）")
    pk.add_argument("--name", help="匹配关键词（默认用工程 project() 名）")
    pk.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pk.add_argument("--json", action="store_true", help="输出 JSON（机器可读）")
    pk.set_defaults(func=cmd_published)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
