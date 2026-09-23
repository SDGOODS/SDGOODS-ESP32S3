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
- **它是开放平台后端的「客户端」**：本身不开源的部分在平台服务端；本文件优先走
  「开发者令牌（sdg_ 开头）经平台托管 MCP（/api/mcp）」通道（与 tools/sdgoods_publish.py
  同一套实现），未配令牌时回退平台公开 REST API，**不含任何密钥**。
- **凭据只在本机**：开发者令牌（api_token）或登录后的 refreshToken 缓存在
  ~/.sdgoods/credentials.json（权限 600），与 sdgoods_publish.py 共用，
  AI 不读取、不回显、不写进仓库。
- 本文件复用 tools/sdgoods_publish.py 的 HTTP / MCP 客户端与上传逻辑，避免重复实现契约。

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


def _read_project_version(file):
    """从固件文件所在目录向上查找 version.txt，读到则返回其（strip 后）内容，否则 None。
    用于 MCP 提交时 version 自动取工程最新版本号，无需用户手填。"""
    d = os.path.dirname(os.path.abspath(file))
    for _ in range(8):
        cand = os.path.join(d, "version.txt")
        if os.path.isfile(cand):
            try:
                with open(cand, "r", encoding="utf-8") as f:
                    return f.read().strip()
            except Exception:
                return None
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return None


def tool_upload_firmware(file, name, category, desc_zh=None, desc_en=None,
                         version=None, hardware=None, tags=None, shots=None,
                         github=None, firmware_id=None, api_base=None):
    """提交（POST 新建）或重新发布（PATCH 更新已有记录，传 firmware_id）。

    鉴权优先级（MCP 访问必须走令牌方式）：
    1. 开发者令牌（SDGOODS_DEV_TOKEN / credentials.json 的 api_token，sdg_ 开头）
       → 走平台托管 MCP（/api/mcp），status 默认 REVIEWING（提审中）；
    2. 否则回退邮箱验证码 cookie 登录 + REST。

    字段策略（用户约定，MCP 提交不再逐项询问）：
    - version：不传则自动从工程 version.txt 读取最新值（找不到回退 v1.0.0）；
    - hardware：默认 sdgoods，不询问；
    - tags：不传则不填；
    - shots：用第 1 步准备好的截图（真机首页图或 AI 图），不询问；
    - status：默认 REVIEWING（提审中），不再用草稿 DRAFT；visible 不填（平台默认 true）。
    重新发布（firmware_id）走 REST PATCH（需 cookie 登录），覆盖旧记录对应字段。
    """
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

    # 分类合法性（公开 REST，无需登录）先校验，省一次无效上传
    cats = pub._valid_categories(base)
    if cats is not None and category not in cats:
        return _err("分类 %r 不存在。先调 list_categories() 核对合法 slug。" % category)

    # ① 开发者令牌通道（推荐）：走平台托管 MCP，status 默认 REVIEWING（提审中）
    token = pub._load_token()
    if token:
        try:
            fw = pub.mcp_upload_firmware(
                base, token, file, name, category,
                desc_zh=desc_zh, desc_en=desc_en,
                version=version or _read_project_version(file),
                hardware=hardware, tags=tags, shots=shots, github=github,
                status="reviewing",
            )
        except RuntimeError as e:
            return _err("MCP 令牌上传失败：%s" % e)
        fid = fw.get("id") if isinstance(fw, dict) else None
        return _ok(
            "提交成功（令牌通道）！固件 id=%s，状态：REVIEWING（提审中，等待审核）。"
            % (fid or "?")
        )

    # ② 回退：邮箱验证码 cookie 登录 + REST（无令牌时使用）
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err(
            "未配置开发者令牌，也未登录。请二选一：\n"
            "  • 令牌通道（推荐）：运行 set-token <sdg_xxx> 或在环境变量设 SDGOODS_DEV_TOKEN；\n"
            "  • 或先调用 login(email=...) 完成邮箱验证码登录。"
        )

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
        # version 自动取工程最新值；找不到才回退 v1.0.0
        "version": version or _read_project_version(file) or "v1.0.0",
        "hardware": hardware or "sdgoods",
        "status": "reviewing",          # 默认提审中（REVIEWING），而非草稿
        "fileName": os.path.basename(file),
        "fileUrl": fw_url,
        "fileSha256": fw_sha,
        "sizeBytes": fw_size,
        "descZh": desc_zh or "",
        "descEn": desc_en or "",
        "shots": shot_urls,
    }
    if tags:
        body["tags"] = tags
    if github:
        body["githubUrl"] = github

    if firmware_id:
        st, payload, _ = pub._req("PATCH", base + "/firmwares/" + str(firmware_id),
                                  token=token, body=body)
        label = "更新（重新发布）"
    else:
        st, payload, _ = pub._req("POST", base + "/firmwares", token=token, body=body)
        label = "提交"
    if st not in (200, 201):
        return _err("%s固件失败（HTTP %s）：%s" % (label, st, pub._err_msg(payload)))
    fw = (payload.get("firmware") if isinstance(payload, dict) else None) or payload
    fid = (fw.get("id") if isinstance(fw, dict) else None) or (firmware_id or "?")
    return _ok(
        "%s成功！固件 id=%s，状态：REVIEWING（提审中）。" % (label, fid)
    )


def tool_list_my_firmwares(api_base=None, q=None, page=None, page_size=None):
    """列出「我的固件」（scope=mine），发布前用来判断是否已经发布过、拿到可复用的 id。"""
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))
    token = pub._load_token()
    if token:
        try:
            return _ok(pub.mcp_my_firmwares(base, token, q=q, page=page, page_size=page_size))
        except RuntimeError as e:
            return _err("MCP 查询失败：%s" % e)
    # 回退：邮箱验证码 cookie 登录 + REST
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err("尚未登录且无开发者令牌。请 set-token 或 login(email=...)。")
    from urllib.parse import quote_plus
    qs = "scope=mine"
    if q:
        qs += "&q=" + quote_plus(q)
    if page:
        qs += "&page=%d" % int(page)
    if page_size:
        qs += "&pageSize=%d" % int(page_size)
    st, payload, _ = pub._req("GET", base + "/firmwares?" + qs, token=token)
    if st not in (200, 201) or not isinstance(payload, dict):
        return _err("获取我的固件列表失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    items = payload.get("items") or []
    return _ok(json.dumps({
        "total": payload.get("total", len(items)),
        "items": items,
    }, ensure_ascii=False, indent=2))


def tool_get_firmware(firmware_id, api_base=None):
    """拉取单条固件记录（name/desc/category/shots/version 等），重新发布时作为草稿。"""
    if not firmware_id:
        return _err("缺少 firmware_id。")
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err("尚未登录。请先调用 login(email=...) 完成登录。")
    st, payload, _ = pub._req("GET", base + "/firmwares/" + str(firmware_id), token=token)
    if st not in (200, 201) or not isinstance(payload, dict):
        return _err("获取固件失败（HTTP %s）：%s" % (st, pub._err_msg(payload)))
    fw = payload.get("firmware") if isinstance(payload, dict) else None
    return _ok(json.dumps(fw, ensure_ascii=False, indent=2))


def tool_whoami(api_base=None):
    try:
        base = _api_base(api_base)
    except RuntimeError as e:
        return _err(str(e))
    token = pub._load_token()
    if token:
        try:
            return _ok(pub.mcp_whoami(base, token))
        except RuntimeError as e:
            return _err("MCP 查询失败：%s" % e)
    # 回退：邮箱验证码 cookie 登录 + REST
    try:
        token = pub._refresh_access(base)
    except SystemExit:
        return _err("尚未登录且无开发者令牌。请 set-token 或 login(email=...)。")
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
            "把编译好的固件 .bin 与信息提交到谷仓 SDGOODS 开放平台。"
            "鉴权默认走「开发者令牌（sdg_ 开头）经平台托管 MCP（/api/mcp）」通道，"
            "提交状态默认 REVIEWING（提审中）；未配令牌时回退邮箱验证码 cookie + REST。"
            "Upload a built firmware via the developer-token MCP channel (status defaults to reviewing)."
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
                "version": {"type": "string",
                            "description": "版本号，≤20 字；不传则自动读取工程 version.txt 最新值"},
                "hardware": {"type": "string",
                             "description": "适用硬件：sdgoods(默认)/cyb1/both，默认 sdgoods 不询问"},
                "tags": {"type": "array", "items": {"type": "string"},
                         "description": "标签，≤12 个，每项 ≤24 字；MCP 提交默认不填"},
                "shots": {"type": "array", "items": {"type": "string"},
                          "description": "截图本地路径，≤4 张；来自发布前第 1 步的准备（真机首页图或 AI 图）"},
                "github": {"type": "string", "description": "GitHub 仓库地址（可选）"},
                "firmware_id": {"type": "string",
                                "description": "已存在固件 id：传了即走 PATCH 更新（重新发布），"
                                              "不传则 POST 新建。重新发布时用 list_my_firmwares 拿到的 id。"},
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": ["file", "name", "category"],
        },
    },
    {
        "name": "list_my_firmwares",
        "description": (
            "列出「我的固件」（scope=mine）。发布前应先用它判断是否已经发布过、拿到可复用的 "
            "固件 id（重新发布时传给 upload_firmware 的 firmware_id 走 PATCH 更新）。"
            "优先走开发者令牌 MCP 通道，未配令牌回退 cookie。Returns items with id/name/version/status."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "q": {"type": "string", "description": "按工程名/名称搜索关键词（如 project() 名）"},
                "page": {"type": "integer", "description": "页码（默认 1）"},
                "page_size": {"type": "integer", "description": "每页条数（默认 12，最大 60）"},
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": [],
        },
    },
    {
        "name": "get_firmware",
        "description": (
            "拉取单条固件记录（name/descZh/descEn/category/tags/shots/version 等）。"
            "重新发布前用它把已发布记录的字段取回来当草稿，再让用户确认是否修改。"
            "Fetch one firmware record as a draft for re-publishing."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "firmware_id": {"type": "string", "description": "固件 id（来自 list_my_firmwares）"},
                "api_base": {"type": "string",
                             "description": "API 基地址，覆盖环境变量 SDGOODS_API_BASE"},
            },
            "required": ["firmware_id"],
        },
    },
    {
        "name": "whoami",
        "description": (
            "返回当前令牌/登录对应的用户（开发者令牌通道优先，未配令牌回退 cookie）。"
            "Show the user for the current token or session."
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
                arguments.get("github"),
                arguments.get("firmware_id"), arguments.get("api_base"))
        elif name == "list_my_firmwares":
            text, is_err = tool_list_my_firmwares(
                arguments.get("api_base"), arguments.get("q"),
                arguments.get("page"), arguments.get("page_size"))
        elif name == "get_firmware":
            text, is_err = tool_get_firmware(
                arguments.get("firmware_id"), arguments.get("api_base"))
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
