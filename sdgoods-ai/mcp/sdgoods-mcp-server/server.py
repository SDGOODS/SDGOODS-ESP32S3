#!/usr/bin/env python3
# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程 · AI 工具链
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
# 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
#   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
# SPDX-License-Identifier: Apache-2.0
#
# 本工具以 Apache-2.0 发布：可自由商用。详见 LICENSING.md。
#
"""SDGOODS 开放平台 · 本地 MCP server（stdio 传输，纯标准库）

让 WorkBuddy / Claude Code / Cursor 等 AI 编码平台以「子进程」方式启动本服务，
直接调用工具把固件上传到「谷仓 SDGOODS 开放平台」（https://sdgoods.ai）。

设计要点：
- **纯标准库**（sys / json / os），无需 pip install，任何装了 Python3 的机器都能跑。
- **stdio 传输**：遵循 MCP 协议（JSON-RPC 2.0 + Content-Length 分帧），由 AI 平台拉起。
- **它是开放平台后端的「客户端」**：本身不开源的部分在平台服务端；本文件只调用
  平台公开 REST API（与 tools/sdgoods_publish.py 同一套端点），**不含任何密钥**。
- **凭据只在本机**：登录后的 refreshToken 缓存在 ~/.sdgoods/credentials.json（权限 600），
  与 sdgoods_publish.py 共用，AI 不读取、不回显、不写进仓库。
- 本文件复用 tools/sdgoods_publish.py 的 HTTP 与上传逻辑，避免重复实现 REST 契约。

安全红线（务必遵守）：
- 绝不在本仓库提交 accessToken / refreshToken / 任何密钥。
- 不要把 SDGOODS_API_BASE 之外的内部地址写死。
"""

import json
import os
import sys

VERSION = "0.1.0"
PROTOCOL_VERSION = "2024-11-05"

# ---------------------------------------------------------------------------
# 复用仓库 tools/sdgoods_publish.py 的 REST 逻辑（单一事实来源）
# ---------------------------------------------------------------------------
_HERE = os.path.dirname(os.path.abspath(__file__))
# server.py 位于 sdgoods-ai/mcp/sdgoods-mcp-server/，仓库根 = 往上三级
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
_TOOLS_DIR = os.path.join(_REPO_ROOT, "tools")
if os.path.isdir(_TOOLS_DIR) and _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

try:
    import sdgoods_publish as pub
except Exception as exc:  # pragma: no cover
    sys.stderr.write("无法加载 tools/sdgoods_publish.py：%s\n" % exc)
    sys.exit(1)


def _api_base(arg=None):
    base = (arg or "").strip() or os.environ.get("SDGOODS_API_BASE", "")
    base = (base or "").rstrip("/")
    if not base:
        raise RuntimeError(
            "缺少 API 基地址：设置环境变量 SDGOODS_API_BASE，"
            "或调用工具时传 api_base 参数（例如 https://sdgoods.ai/api）"
        )
    return base


# ---------------------------------------------------------------------------
# 工具实现：每个返回 (text, is_error)
# ---------------------------------------------------------------------------
def _ok(text):
    return (text, False)


def _err(text):
    return (text, True)


def tool_login(email, code=None, api_base=None):
    """邮箱验证码登录。两步：先不带 code（发码），收到码后再带 code 调用。"""
    if not email or "@" not in email:
        return _err("请提供合法邮箱（email 参数）。")
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))

    if not code:
        st, _, _ = pub._req("POST", base + "/auth/code", body={"email": email})
        if st not in (200, 201, 202, 204):
            return _err("请求验证码失败（HTTP %s）。检查邮箱格式与 SDGOODS_API_BASE。" % st)
        return _ok(
            "验证码已发送到 %s，请查收 4 位数字。\n"
            "收码后再次调用 login(email=%r, code=\"验证码\") 完成登录。"
            % (email, email)
        )

    st, payload, headers = pub._req(
        "POST", base + "/auth/verify", body={"email": email, "code": str(code)}
    )
    if st not in (200, 201):
        return _err("登录失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    refresh = pub._extract_refresh_token(
        [headers["Set-Cookie"]] if headers.get("Set-Cookie") else []
    )
    if not refresh and isinstance(payload, dict):
        refresh = payload.get("refreshToken")
    if not refresh:
        return _err("登录成功但拿不到 refreshToken，无法缓存凭据，请稍后重试。")
    pub.save_creds({"api_base": base, "email": email, "refresh_token": refresh})
    who = (payload.get("user") or {}).get("handle") or email
    return _ok("登录成功（%s）。凭据已缓存到 %s，之后提交无需再次验证码。" % (who, pub.CRED_FILE))


def tool_list_categories(api_base=None):
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))
    st, payload, _ = pub._req("GET", base + "/categories")
    if st not in (200, 201) or not isinstance(payload, dict):
        return _err("获取分类失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    cats = payload.get("categories") or []
    if not cats:
        return _ok("平台暂未返回分类（空）。提交前请到网页端确认分类 slug。")
    lines = ["合法分类 slug（提交 upload_firmware 时用）："]
    for c in cats:
        if isinstance(c, dict):
            lines.append("  %s — %s / %s"
                         % (c.get("slug"), c.get("nameZh"), c.get("nameEn")))
    return _ok("\n".join(lines))


def tool_upload_firmware(file, name, category, desc_zh=None, desc_en=None,
                         version=None, hardware=None, tags=None, shots=None,
                         github=None, draft=False, api_base=None):
    if not file or not os.path.isfile(file):
        return _err("固件文件不存在：%s（file 参数需为本地 .bin 路径）" % file)
    if not name:
        return _err("缺少固件名称（name 参数）。")
    if not category:
        return _err("缺少分类（category 参数）。先调 list_categories() 取合法 slug。")
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))

    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err("尚未登录。请先调用 login(email=...) 完成邮箱验证码登录。")

    cats = pub._valid_categories(base)
    if cats is not None and category not in cats:
        return _err("分类 %r 不存在。先调 list_categories() 核对合法 slug。" % category)

    shot_urls = []
    for s in (shots or [])[:4]:
        if not os.path.isfile(s):
            return _err("截图文件不存在：%s" % s)
        shot_urls.append(pub._presign_and_put(base, token, "shot", s))

    try:
        fw_url = pub._presign_and_put(base, token, "firmware", file)
    except SystemExit:
        return _err("固件上传失败（见上方服务端信息）。")
    fw_sha = pub._sha256_hex(file)
    fw_size = os.path.getsize(file)

    body = {
        "name": name,
        "category": category,
        "version": version or "v1.0.0",
        "hardware": hardware or "sdgoods",
        "status": "draft" if draft else "reviewing",
        "visible": True,
        "fileName": os.path.basename(file),
        "fileUrl": fw_url,
        "fileSha256": fw_sha,
        "sizeBytes": fw_size,
        "descZh": desc_zh or "",
        "descEn": desc_en or "",
        "tags": tags or [],
        "shots": shot_urls,
    }
    if github:
        body["githubUrl"] = github

    st, payload, _ = pub._req("POST", base + "/firmwares", token=token, body=body)
    if st not in (200, 201):
        return _err("提交固件失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    fw = (payload.get("firmware") if isinstance(payload, dict) else None) or payload
    fid = (fw.get("id") if isinstance(fw, dict) else None) or "?"
    return _ok(
        "提交成功！固件 id=%s（状态：%s）。\n可在「我的固件」里查看、补充信息或提交审核。"
        % (fid, body["status"])
    )


def tool_whoami(api_base=None):
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err("尚未登录。请先调用 login(email=...) 完成登录。")
    st, payload, _ = pub._req("GET", base + "/auth/me", token=token)
    if st not in (200, 201):
        return _err("获取当前用户失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    return _ok(json.dumps(payload, ensure_ascii=False, indent=2))


def tool_logout():
    creds = pub.load_creds()
    if creds:
        try:
            os.remove(pub.CRED_FILE)
        except Exception:
            pass
        return _ok("已清除本机缓存的凭据（%s）。" % pub.CRED_FILE)
    return _ok("本机没有缓存的凭据，无需清除。")


# ---------------------------------------------------------------------------
# 工具清单（MCP tools/list）
# ---------------------------------------------------------------------------
TOOLS = [
    {
        "name": "login",
        "description": (
            "邮箱验证码登录谷仓 SDGOODS 开放平台。两步：第一次不带 code（向邮箱发 4 位码），"
            "收到后第二次带 code 调用即完成登录。凭据缓存在本机 ~/.sdgoods/credentials.json。"
            "Login to SDGOODS Open Platform via email code (two-step)."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "email": {"type": "string", "description": "注册/登录邮箱"},
                "code": {"type": "string",
                         "description": "4 位验证码；首次调用不传，收到码后再次调用传入"},
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": ["email"],
        },
    },
    {
        "name": "list_categories",
        "description": (
            "列出平台已有的固件分类 slug（提交 upload_firmware 时必须用合法 slug）。"
            "List valid firmware category slugs from the Open Platform."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": [],
        },
    },
    {
        "name": "upload_firmware",
        "description": (
            "把编译好的固件 .bin 与信息提交到谷仓 SDGOODS 开放平台（presign 直传 + 创建记录）。"
            "登录态会自动用本机缓存的 refreshToken 续期。Upload a built firmware to the platform."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "description": "固件 .bin 的本地路径（必填）"},
                "name": {"type": "string", "description": "固件名称，≤60 字（必填）"},
                "category": {"type": "string",
                             "description": "分类 slug，须为 list_categories() 返回值之一（必填）"},
                "desc_zh": {"type": "string", "description": "中文简介，≤2000 字"},
                "desc_en": {"type": "string", "description": "英文简介，≤2000 字"},
                "version": {"type": "string", "description": "版本号，默认 v1.0.0，≤20 字"},
                "hardware": {"type": "string",
                             "description": "适用硬件：sdgoods(默认)/cyb1/both"},
                "tags": {"type": "array", "items": {"type": "string"},
                         "description": "标签，≤12 个，每项 ≤24 字"},
                "shots": {"type": "array", "items": {"type": "string"},
                          "description": "截图本地路径，≤4 张"},
                "github": {"type": "string", "description": "GitHub 仓库地址（可选）"},
                "draft": {"type": "boolean",
                          "description": "true=存为草稿，false(默认)=提交审核"},
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": ["file", "name", "category"],
        },
    },
    {
        "name": "whoami",
        "description": (
            "返回当前登录用户（需先 login）。Show the currently logged-in user."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": [],
        },
    },
    {
        "name": "logout",
        "description": (
            "清除本机缓存的开放平台凭据（~/.sdgoods/credentials.json）。"
            "Clear locally cached Open Platform credentials."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
]


def dispatch(name, arguments):
    arguments = arguments or {}
    try:
        if name == "login":
            text, is_err = tool_login(
                arguments.get("email"), arguments.get("code"), arguments.get("api_base"))
        elif name == "list_categories":
            text, is_err = tool_list_categories(arguments.get("api_base"))
        elif name == "upload_firmware":
            text, is_err = tool_upload_firmware(
                arguments.get("file"), arguments.get("name"), arguments.get("category"),
                arguments.get("desc_zh"), arguments.get("desc_en"),
                arguments.get("version"), arguments.get("hardware"),
                arguments.get("tags"), arguments.get("shots"),
                arguments.get("github"), bool(arguments.get("draft", False)),
                arguments.get("api_base"))
        elif name == "whoami":
            text, is_err = tool_whoami(arguments.get("api_base"))
        elif name == "logout":
            text, is_err = tool_logout()
        else:
            text, is_err = _err("未知工具：%s" % name)
    except Exception as e:  # 任何未预期异常都转成 isError，绝不崩
        text, is_err = _err("工具执行异常：%s" % e)
    return {"content": [{"type": "text", "text": text}], "isError": is_err}


# ---------------------------------------------------------------------------
# MCP stdio 传输（JSON-RPC 2.0 + Content-Length 分帧）
# ---------------------------------------------------------------------------
def _send(obj):
    data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(data))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def _read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None  # EOF：AI 平台关闭了 stdin
        if line == b"\r\n":
            break
        if b":" in line:
            k, _, v = line.partition(b":")
            headers[k.strip().lower().decode("utf-8", "replace")] = v.strip().decode("utf-8", "replace")
    try:
        n = int(headers.get("content-length", "0"))
    except ValueError:
        n = 0
    if n <= 0:
        return None
    body = b""
    while len(body) < n:
        chunk = sys.stdin.buffer.read(n - len(body))
        if not chunk:
            return None
        body += chunk
    try:
        return json.loads(body.decode("utf-8", "replace"))
    except Exception:
        return None


def main():
    while True:
        msg = _read_message()
        if msg is None:
            break
        method = msg.get("method")
        mid = msg.get("id")

        if method == "initialize":
            _send({
                "jsonrpc": "2.0", "id": mid,
                "result": {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "sdgoods", "version": VERSION},
                },
            })
        elif method == "notifications/initialized":
            continue
        elif method == "ping":
            _send({"jsonrpc": "2.0", "id": mid, "result": {}})
        elif method == "tools/list":
            _send({"jsonrpc": "2.0", "id": mid, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            params = msg.get("params", {})
            name = params.get("name", "")
            result = dispatch(name, params.get("arguments"))
            _send({"jsonrpc": "2.0", "id": mid, "result": result})
        else:
            # 未知方法：若是带 id 的请求，回 method not found；通知则忽略
            if mid is not None:
                _send({
                    "jsonrpc": "2.0", "id": mid,
                    "error": {"code": -32601, "message": "method not found: %s" % method},
                })


if __name__ == "__main__":
    main()
