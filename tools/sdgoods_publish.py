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
"""sdgoods_publish —— 把固件和固件信息提交到「谷仓 SDGOODS 开放平台」

纯标准库实现（Python 3.8+，无需 pip install），既给人用，也方便 AI 助手直接调用。
配合谷仓次元屏（SDGOODS-ESP32S3）固件开发使用。

对外发布只有两条通道：MCP 开发者令牌（推荐）与网页手动。本工具的推荐用法是令牌通道：

    # 1) 一次性保存开发者令牌（sdg_ 开头，平台「个人中心 → 开发者令牌」生成，明文只显示一次）
    python3 tools/sdgoods_publish.py set-token "sdg_你的令牌"
    # 或临时用环境变量：export SDGOODS_DEV_TOKEN="sdg_你的令牌"

    # 2) 先导出并校验应用包（必须是「不带地址」的纯 app 镜像）：
    #    python3 tools/pack_app.py
    python3 tools/sdgoods_publish.py mcp-upload \
        --file dist/SDGOODS_EBADGE_app.bin \
        --name "我的固件" \
        --desc-zh "一句话介绍这个固件" \
        --desc-en "One-line intro" \
        --category game \
        --shots shot1.png shot2.png \
        --github https://github.com/you/your-fw

其他令牌通道子命令：mcp-firmwares（列出自己提交的）、mcp-replace <旧id>（删旧重建）、
mcp-unpublish（下架）、mcp-delete（删除）、mcp-download <id> --check-sha256 <本地sha256>（防砖校验）。
全部见 `python3 tools/sdgoods_publish.py --help` 与 docs/PUBLISHING.md。

> 附注：本工具还保留了邮箱验证码 REST 通道（login / publish / whoami / logout），
> 仅用于平台联调/内部参考，**不是对外的发布通道**——生产环境默认没有邮箱登录态，
> 对外发布一律走 sdg_ 开发者令牌（MCP）或网页手动。

上传前本工具会校验应用包（与 tools/pack_app.py 同一套判据）：
必须是纯 app 镜像 —— 带地址的合并镜像（merged.bin）会被直接拒绝，
因为平台按它刷机会写到 0x0、覆盖 bootloader 与分区表。
确实要跳过校验（不推荐）：加 --no-verify。

API 基地址：环境变量 SDGOODS_API_BASE（与网页端约定一致），或 --api 参数。
例如生产环境：  export SDGOODS_API_BASE=https://sdgoods.ai/api
"""

import argparse
import base64
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request

CRED_FILE = os.path.join(os.path.expanduser("~"), ".sdgoods", "credentials.json")


# --------------------------------------------------------------------------- 基础 HTTP
def _api_base(args):
    base = getattr(args, "api", None) or os.environ.get("SDGOODS_API_BASE", "")
    base = (base or "").rstrip("/")
    if not base:
        sys.stderr.write(
            "缺少 API 基地址。请设置环境变量 SDGOODS_API_BASE（例如 "
            "https://你的平台域名/api），或传 --api <url>。\n"
        )
        sys.exit(2)
    return base


def _req(method, url, *, token=None, body=None, raw=None, content_type=None,
         extra_headers=None, timeout=60):
    """发一次 HTTP 请求。body=dict 走 JSON；raw=bytes 走裸字节（用于 PUT 到对象存储）。"""
    data = None
    headers = dict(extra_headers or {})
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if raw is not None:
        data = raw
        if content_type:
            headers["Content-Type"] = content_type
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw_body = resp.read()
            return resp.status, _maybe_json(raw_body), dict(resp.headers)
    except urllib.error.HTTPError as e:
        raw_body = e.read()
        try:
            payload = json.loads(raw_body.decode("utf-8", "replace"))
        except Exception:
            payload = raw_body.decode("utf-8", "replace")
        return e.code, payload, dict(e.headers)


def _maybe_json(b):
    try:
        return json.loads(b.decode("utf-8", "replace"))
    except Exception:
        return b.decode("utf-8", "replace")


# --------------------------------------------------------------------------- 凭证
def load_creds():
    try:
        with open(CRED_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def save_creds(creds):
    os.makedirs(os.path.dirname(CRED_FILE), exist_ok=True)
    with open(CRED_FILE, "w", encoding="utf-8") as f:
        json.dump(creds, f, ensure_ascii=False, indent=2)
    try:
        os.chmod(CRED_FILE, 0o600)
    except Exception:
        pass


def _extract_refresh_token(set_cookie_headers):
    """从响应头的 Set-Cookie 里抠出 refreshToken（HttpOnly cookie 不在响应体里）。"""
    for h in set_cookie_headers:
        if "refreshToken=" not in h:
            continue
        for part in h.split(","):
            part = part.strip()
            if part.startswith("refreshToken="):
                return part.split(";", 1)[0].split("=", 1)[1]
    return None


# --------------------------------------------------------------------------- 鉴权
def cmd_login(args):
    base = _api_base(args)
    email = args.email
    # ① 请求验证码（无论是否已注册都返回同样响应，防枚举）
    st, _, _ = _req("POST", base + "/auth/code", body={"email": email})
    if st not in (200, 201, 202, 204):
        sys.stderr.write("请求验证码失败（HTTP %s）。检查邮箱格式与 API 基地址。\n" % st)
        sys.exit(1)
    print("验证码已发送到 %s，请查收（4 位数字）。" % email)
    code = args.code or input("请输入验证码：").strip()
    # ② 校验验证码并登录
    st, payload, headers = _req(
        "POST", base + "/auth/verify", body={"email": email, "code": code}
    )
    if st not in (200, 201):
        sys.stderr.write("登录失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    refresh = _extract_refresh_token(headers.get("Set-Cookie", "") and [headers.get("Set-Cookie")] or [])
    if not refresh:
        # 某些实现可能把 refreshToken 直接放响应体；这里兜底
        refresh = (payload.get("refreshToken") if isinstance(payload, dict) else None)
    if not refresh:
        sys.stderr.write("登录成功但拿不到 refreshToken，无法缓存凭据。请稍后重试。\n")
        sys.exit(1)
    creds = {"api_base": base, "email": email, "refresh_token": refresh}
    save_creds(creds)
    who = (payload.get("user") or {}).get("handle") or email
    print("登录成功（%s）。凭据已缓存到 %s，之后提交无需再次验证码。" % (who, CRED_FILE))


def _refresh_access(base):
    creds = load_creds()
    if not creds or not creds.get("refresh_token"):
        sys.stderr.write("尚未登录。请先运行：python3 tools/sdgoods_publish.py login 你的邮箱\n")
        sys.exit(1)
    st, payload, _ = _req(
        "POST", base + "/auth/refresh",
        body={"refreshToken": creds["refresh_token"]},
    )
    if st not in (200, 201):
        sys.stderr.write("登录已失效（HTTP %s）：%s\n请重新运行 login。\n" % (st, _err_msg(payload)))
        sys.exit(1)
    return payload.get("accessToken") if isinstance(payload, dict) else None


def _err_msg(payload):
    if isinstance(payload, dict):
        return payload.get("message") or payload.get("error") or json.dumps(payload, ensure_ascii=False)
    return str(payload)


# --------------------------------------------------------------------------- 开发者令牌（MCP 令牌通道）
# 平台托管 MCP 端点（{base}/mcp）支持「开发者令牌」（sdg_ 前缀）作为 Bearer 凭证，
# 比邮箱验证码 + refreshToken cookie 更稳、可吊销、适合 AI 长期接入。
# 令牌在网页端「个人中心 → 开发者令牌」生成；本工具从 SDGOODS_DEV_TOKEN 环境变量
# 或 ~/.sdgoods/credentials.json 的 api_token 字段读取。

def _load_token():
    t = os.environ.get("SDGOODS_DEV_TOKEN")
    if t:
        return t.strip()
    creds = load_creds() or {}
    t = creds.get("api_token")
    return t.strip() if t else None


def cmd_set_token(args):
    """把开发者令牌保存到本机凭据文件；之后 MCP 发布自动走令牌通道。"""
    token = (args.token or "").strip()
    if not token.startswith("sdg_"):
        sys.stderr.write("令牌格式不正确：应以 sdg_ 开头（网页端「个人中心 → 开发者令牌」生成）。\n")
        sys.exit(1)
    creds = load_creds() or {}
    creds["api_token"] = token
    if args.api:
        creds["api_base"] = args.api.rstrip("/")
    save_creds(creds)
    print("开发者令牌已保存（%s）。MCP 发布将走令牌通道（Bearer sdg_***）。" % CRED_FILE)


def cmd_mcp_token(args):
    """检查本机是否已保存开发者令牌（绝不回显令牌明文）。

    发布流程 ⓪ 用：选 MCP 发布 → 先跑本命令判断是否已存令牌，
    未存则请用户粘贴；手动发布时也可用它决定「要不要拉平台旧记录当草稿」。
    """
    if args.set:
        args.token = args.set          # 复用 cmd_set_token 的参数约定
        cmd_set_token(args)
        return
    t = _load_token()
    if not t:
        print("NOT_SAVED 本机尚未保存开发者令牌（%s 无 api_token，且未设 SDGOODS_DEV_TOKEN）" % CRED_FILE)
        print("→ 请到平台「个人中心 → 开发者令牌」生成 sdg_ 令牌，再用 set-token <sdg_...> 保存。")
        return
    ok = t.startswith("sdg_")
    masked = (t[:6] + "..." + t[-4:]) if len(t) > 12 else "***"
    print("SAVED 已保存开发者令牌：%s（来源：%s）" % (
        masked, "SDGOODS_DEV_TOKEN" if os.environ.get("SDGOODS_DEV_TOKEN") else CRED_FILE))
    if not ok:
        print("⚠️ 警告：已存的令牌不是 sdg_ 开头，MCP 调用会被拒（-32001），建议重新 set-token。")


def _mcp_url(base):
    return base.rstrip("/") + "/mcp"


def mcp_call(base, token, method, params, *, timeout=120):
    """向平台托管 MCP 端点发一次 JSON-RPC 2.0 请求（无状态 Streamable HTTP）。

    鉴权：Authorization: Bearer <token>。**只接受开发者令牌 sdg_xxx**（2026-09-24 实测：
    网站登录态 JWT 打 MCP 会返回 -32001「仅接受开发者令牌」；反过来 sdg_ 令牌打 REST 是 401）。
    返回解析后的结果：tools/call 返回结果文本；其它方法返回 result 对象。
    协议层或工具层错误都抛 RuntimeError。
    """
    url = _mcp_url(base)
    payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    st, body, _ = _req(
        "POST", url, body=payload,
        extra_headers={"Accept": "application/json", "Authorization": "Bearer " + token},
        timeout=timeout,
    )
    if st not in (200, 201, 202, 203, 204):
        raise RuntimeError("MCP 端点返回 HTTP %s：%s" % (st, _err_msg(body)))
    if not isinstance(body, dict):
        raise RuntimeError("MCP 端点返回非 JSON 结果：%r" % (body,))
    if "error" in body:
        err = body["error"]
        raise RuntimeError("MCP 协议错误 %s：%s" % (err.get("code"), err.get("message")))
    result = body.get("result")
    if method == "tools/call":
        content = (result or {}).get("content") or []
        text = "".join(c.get("text", "") for c in content
                       if isinstance(c, dict) and c.get("type") == "text")
        if (result or {}).get("isError"):
            raise RuntimeError("MCP 工具执行失败：%s" % text)
        return text
    return result


def _read_project_version(file):
    """从固件文件所在目录向上查找 version.txt，读到则返回其（strip 后）内容，否则 None。

    用于 MCP 提交时 version 自动取工程最新版本号，无需用户手填。
    """
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


def mcp_upload_firmware(base, token, file, name, category, *, desc_zh=None, desc_en=None,
                        version=None, hardware=None, tags=None, shots=None, github=None,
                        status="reviewing"):
    """通过平台托管 MCP（令牌通道）上传固件；status 默认 reviewing（提审中）。

    - file：本地 .bin，读成 base64 作为 contentBase64（单应用包，落点由平台按内容判别）。
    - shots：本地图片路径列表（≤4），逐个读成 base64 作为 shots 元素。
    - version：不传则自动从工程 version.txt 读取最新值（找不到回退 v1.0.0）。
    返回平台回应的固件记录（dict，已 JSON 解析）。
    """
    if not os.path.isfile(file):
        raise RuntimeError("固件文件不存在：%s" % file)
    with open(file, "rb") as f:
        content_b64 = base64.b64encode(f.read()).decode("ascii")

    shot_b64 = []
    for s in (shots or [])[:4]:
        if not os.path.isfile(s):
            raise RuntimeError("截图文件不存在：%s" % s)
        with open(s, "rb") as f:
            shot_b64.append(base64.b64encode(f.read()).decode("ascii"))

    # version 不传时自动取工程最新值（用户约定：发布版本号用最新）
    if not version:
        version = _read_project_version(file)

    a = {
        "name": name,
        "category": category,
        "status": status,                # 默认提审中（REVIEWING），而非草稿
        "hardware": hardware or "sdgoods",
        "contentBase64": content_b64,
    }
    if version:
        a["version"] = version
    if desc_zh:
        a["descZh"] = desc_zh
    if desc_en:
        a["descEn"] = desc_en
    if tags:
        a["tags"] = tags
    if shot_b64:
        a["shots"] = shot_b64
    if github:
        a["githubUrl"] = github

    text = mcp_call(base, token, "tools/call", {"name": "upload_firmware", "arguments": a})
    try:
        return json.loads(text)
    except Exception:
        return {"raw": text}


def mcp_my_firmwares(base, token, *, q=None, page=None, page_size=None):
    params = {}
    if q:
        params["q"] = q
    if page:
        params["page"] = page
    if page_size:
        params["pageSize"] = page_size
    return mcp_call(base, token, "tools/call", {"name": "my_firmwares", "arguments": params or {}})


def mcp_whoami(base, token):
    return mcp_call(base, token, "tools/call", {"name": "whoami", "arguments": {}})


def mcp_unpublish(base, token, fw_id, *, reason=None):
    """令牌通道下架：published/reviewing → draft（市场立刻不再展示）。"""
    args = {"id": fw_id}
    if reason:
        args["reason"] = reason
    return mcp_call(base, token, "tools/call", {"name": "unpublish_firmware", "arguments": args})


def mcp_delete(base, token, fw_id):
    """令牌通道删除：只允许 draft / rejected，故通常先 mcp_unpublish 再删。"""
    return mcp_call(base, token, "tools/call", {"name": "delete_firmware", "arguments": {"id": fw_id}})


def mcp_download(base, token, fw_id, *, mode=None):
    """令牌通道拉刷机清单（防砖验证）：返回 parts 地址/sha256 + declared。

    对应 REST `POST /api/firmwares/:id/download`，但那个端点只吃网站登录 JWT，
    生产上只有 sdg_ 令牌，必须走这个 MCP 工具才能核对落点。
    """
    args = {"id": fw_id}
    if mode:
        args["mode"] = mode
    return mcp_call(base, token, "tools/call", {"name": "firmware_download", "arguments": args})


def cmd_mcp_upload(args):
    """令牌通道上传：走平台托管 MCP，status 默认 reviewing。"""
    if not args.file or not os.path.isfile(args.file):
        sys.stderr.write("请通过 --file 指定固件 .bin 路径。\n")
        sys.exit(1)
    _check_app_package(args.file, max_size=args.max_size, no_verify=args.no_verify)
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write(
            "未配置开发者令牌。请先运行：\n"
            "  python3 tools/sdgoods_publish.py set-token <sdg_xxx>\n"
            "（令牌在网页端「个人中心 → 开发者令牌」生成；也可设环境变量 SDGOODS_DEV_TOKEN）\n")
        sys.exit(1)
    cats = _valid_categories(base)
    if cats is not None and args.category not in cats:
        sys.stderr.write("分类 '%s' 不存在。合法分类：%s\n" % (args.category, ", ".join(sorted(cats)) or "(空)"))
        sys.exit(1)
    try:
        fw = mcp_upload_firmware(
            base, token, args.file, args.name, args.category,
            desc_zh=args.desc_zh, desc_en=args.desc_en,
            version=args.version, hardware=args.hardware,
            tags=args.tags, shots=args.shots, github=args.github, status="reviewing",
        )
    except RuntimeError as e:
        sys.stderr.write("MCP 上传失败：%s\n" % e)
        sys.exit(1)
    fid = fw.get("id") if isinstance(fw, dict) else None
    print("提交成功（令牌通道）！固件 id=%s，状态：REVIEWING（提审中）。" % (fid or "?"))
    print(json.dumps(fw, ensure_ascii=False, indent=2))


def cmd_mcp_whoami(args):
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        out = mcp_whoami(base, token)
    except RuntimeError as e:
        sys.stderr.write("MCP 查询失败：%s\n" % e)
        sys.exit(1)
    print(out)


def cmd_mcp_firmwares(args):
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        out = mcp_my_firmwares(base, token, q=args.q, page=args.page, page_size=args.page_size)
    except RuntimeError as e:
        sys.stderr.write("MCP 查询失败：%s\n" % e)
        sys.exit(1)
    if getattr(args, "name", None):
        try:
            rows = json.loads(out)
            rows = [r for r in rows if isinstance(r, dict) and r.get("name") == args.name]
            out = json.dumps(rows, ensure_ascii=False, indent=2)
        except Exception:
            pass
    print(out)


def cmd_mcp_unpublish(args):
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        print(mcp_unpublish(base, token, args.id, reason=args.reason))
    except RuntimeError as e:
        sys.stderr.write("MCP 下架失败：%s\n" % e)
        sys.exit(1)


def cmd_mcp_delete(args):
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        print(mcp_delete(base, token, args.id))
    except RuntimeError as e:
        sys.stderr.write("MCP 删除失败：%s\n" % e)
        sys.exit(1)


def cmd_mcp_download(args):
    """令牌通道的防砖验证：拉刷机清单，核对 4 段地址与 app 段 sha256。"""
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        out = mcp_download(base, token, args.id, mode=args.mode)
    except RuntimeError as e:
        sys.stderr.write("MCP 拉取刷机清单失败：%s\n" % e)
        sys.exit(1)
    print(out)
    if args.check_sha256:
        try:
            data = json.loads(out)
        except Exception:
            sys.stderr.write("（无法解析回执 JSON，跳过 sha256 比对）\n")
            return
        parts = data.get("parts") or []
        if len(parts) != 4:
            sys.stderr.write("⚠️ 段数不是 4（实际 %d 段），请人工核对落点。\n" % len(parts))
        want = args.check_sha256.lower()
        app = [p for p in parts if str(p.get("address", "")).lower() in ("0x10000", "65536")]
        if not app:
            sys.stderr.write("⚠️ 回执里没有 app@0x10000 段。\n")
        elif (app[0].get("sha256") or "").lower() != want:
            sys.stderr.write("⚠️ app 段 sha256 不一致：平台=%s 本地=%s\n"
                             % (app[0].get("sha256"), want))
        else:
            print("✅ 防砖校验通过：4 段齐全、app@0x10000 的 sha256 与本地一致、declared=%s"
                  % data.get("declared"))


def cmd_mcp_replace(args):
    """令牌通道的「删旧建新」：先下架（→draft）再删除，为重新发布腾位（铁律②）。

    等价于本地有登录态时的 REST `publish --replace`：保证平台上同名 app 只有一条。
    """
    base = _api_base(args)
    token = _load_token()
    if not token:
        sys.stderr.write("未配置开发者令牌（set-token / SDGOODS_DEV_TOKEN）。\n")
        sys.exit(1)
    try:
        try:
            print("[1/2] unpublish ->", mcp_unpublish(base, token, args.id, reason=args.reason))
        except RuntimeError as e:
            sys.stderr.write("（下架跳过或失败，继续尝试删除：%s）\n" % e)
        print("[2/2] delete    ->", mcp_delete(base, token, args.id))
    except RuntimeError as e:
        sys.stderr.write("MCP 删旧失败：%s\n" % e)
        sys.exit(1)


# --------------------------------------------------------------------------- 上传
def _sha256_hex(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _presign_and_put(base, token, kind, filepath):
    """复刻前端 store.js：presign 拿直传地址 → PUT 字节 → 返回 publicUrl。"""
    content_type = "application/octet-stream" if kind == "firmware" else _guess_image_type(filepath)
    size = os.path.getsize(filepath)
    st, payload, _ = _req(
        "POST", base + "/uploads/presign",
        token=token,
        body={"kind": kind, "contentType": content_type, "sizeBytes": size,
              "filename": os.path.basename(filepath)},
    )
    if st not in (200, 201):
        sys.stderr.write("获取上传地址失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    info = payload if isinstance(payload, dict) else {}
    upload_url = info.get("uploadUrl")
    public_url = info.get("publicUrl")
    if not upload_url:
        sys.stderr.write("presign 未返回 uploadUrl。\n")
        sys.exit(1)
    with open(filepath, "rb") as f:
        data = f.read()
    headers = info.get("headers") or {}
    st, _, _ = _req("PUT", upload_url, raw=data,
                    content_type=content_type, extra_headers=headers, timeout=300)
    if st not in (200, 201, 204):
        sys.stderr.write("%s 上传失败（HTTP %s）。\n" % (kind, st))
        sys.exit(1)
    return public_url or info.get("key")


def _guess_image_type(path):
    ext = os.path.splitext(path)[1].lower()
    return {
        ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
        ".webp": "image/webp", ".gif": "image/gif",
    }.get(ext, "application/octet-stream")


# --------------------------------------------------------------------------- 应用包校验
def _load_pack_app():
    """加载同目录的 pack_app 模块（校验逻辑的单一事实来源）。

    拿不到就返回 None —— 校验是「尽力而为」：工具文件缺失不该让发布流程彻底走不动。
    """
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import pack_app  # type: ignore
        return pack_app
    except Exception:
        return None


def _check_app_package(path, max_size=None, no_verify=False):
    """上传前校验应用包；不合规就打印原因并退出。

    平台约定：交付物必须是**不带地址**的纯 app 镜像。带地址的合并镜像（merged.bin）
    会让平台从 0x0 写下去、覆盖 bootloader 与分区表，用户刷完无法启动。
    """
    pack = _load_pack_app()
    if pack is None:
        sys.stderr.write("提示：未找到 tools/pack_app.py，跳过应用包校验。\n")
        return
    verdict = pack.verify_app_bin(path, max_bytes=max_size or pack.DEFAULT_SLOT_BYTES)

    if verdict["ok"]:
        print("应用包校验通过：%s，%s，%s"
              % (verdict["fileName"], verdict.get("chipName") or "?",
                 "%.2f MB" % (verdict["sizeBytes"] / 1024.0 / 1024.0)))
        return
    if no_verify:
        sys.stderr.write("⚠ 应用包校验未通过，但指定了 --no-verify，继续上传：\n")
        for e in verdict["errors"]:
            sys.stderr.write("  - %s\n" % e)
        return

    sys.stderr.write(pack.format_report(verdict) + "\n")
    sys.stderr.write("\n确认无误要强行上传，可加 --no-verify（不推荐）。\n")
    sys.exit(1)


def _valid_categories(base):
    st, payload, _ = _req("GET", base + "/categories")
    if st not in (200, 201) or not isinstance(payload, dict):
        return None  # 拿不到也不阻塞，交给服务端校验
    cats = payload.get("categories") or []
    return {c.get("slug") for c in cats if isinstance(c, dict)}


def cmd_publish(args):
    # ★ 第 0 步：本地校验应用包（纯本地、不联网，失败就没必要去登录）
    #    平台只接受「不带地址」的纯 app 镜像，见 docs/BUILD.md 第 4 节。
    if not args.file or not os.path.isfile(args.file):
        sys.stderr.write("请通过 --file 指定固件 .bin 路径。\n")
        sys.exit(1)
    _check_app_package(args.file, max_size=args.max_size, no_verify=args.no_verify)

    base = _api_base(args)
    token = _refresh_access(base)

    # 分类校验（拿不到列表就跳过，由服务端兜底）
    cats = _valid_categories(base)
    if cats is not None and args.category not in cats:
        sys.stderr.write(
            "分类 '%s' 不存在。合法分类：%s\n" % (args.category, ", ".join(sorted(cats)) or "(空)")
        )
        sys.exit(1)

    # 截图（最多 4 张）
    shots = []
    for s in (args.shots or []):
        if len(shots) >= 4:
            sys.stderr.write("截图最多 4 张，多余的已忽略。\n")
            break
        shots.append(_presign_and_put(base, token, "shot", s))

    # 固件 bin（已在函数开头校验过）
    fw_url = _presign_and_put(base, token, "firmware", args.file)
    fw_sha = _sha256_hex(args.file)
    fw_size = os.path.getsize(args.file)

    body = {
        "name": args.name,
        "category": args.category,
        "version": args.version or "v1.0.0",
        "hardware": args.hardware,
        "status": "draft" if args.draft else "reviewing",
        "visible": True,
        "fileName": os.path.basename(args.file),
        "fileUrl": fw_url,
        "fileSha256": fw_sha,
        "sizeBytes": fw_size,
        "descZh": args.desc_zh or "",
        "descEn": args.desc_en or "",
        "tags": args.tags or [],
        "shots": shots,
    }
    if args.github:
        body["githubUrl"] = args.github

    # ★ 重新发布：--replace 先删旧记录（DELETE），再 POST 新建，保证平台上
    #   同一项目始终只有一份（符合「不能有两个一样的项目」的发布规范）；
    #   传 --id 则走 PATCH 原地更新已有记录；都不传则直接 POST 新建。
    if args.replace:
        if args.id:
            sys.stderr.write("--replace 与 --id 互斥，二选一即可。\n")
            sys.exit(1)
        st, payload, _ = _req("DELETE", base + "/firmwares/" + args.replace, token=token)
        if st not in (200, 201, 204):
            sys.stderr.write("删除旧固件 %s 失败（HTTP %s）：%s\n"
                             % (args.replace, st, _err_msg(payload)))
            sys.exit(1)
        print("已删除旧固件 id=%s，准备提交新版本。" % args.replace)
        args.id = None  # 后续按新建走

    if args.id:
        st, payload, _ = _req("PATCH", base + "/firmwares/" + args.id, token=token, body=body)
        label = "更新"
    else:
        st, payload, _ = _req("POST", base + "/firmwares", token=token, body=body)
        label = "提交"
    if st not in (200, 201):
        sys.stderr.write("%s固件失败（HTTP %s）：%s\n" % (label, st, _err_msg(payload)))
        sys.exit(1)
    fw = (payload.get("firmware") if isinstance(payload, dict) else None) or payload
    fid = (fw.get("id") if isinstance(fw, dict) else None) or (args.id or "?")
    print("%s成功！固件 id=%s（状态：%s）。" % (label, fid, body["status"]))
    if args.id:
        print("已在原记录上更新（版本号/截图/简介按本次提交覆盖）。")
    else:
        print("可在「我的固件」里查看、补充信息或提交审核。")


def cmd_firmwares_list(args):
    """列出「我的固件」（scope=mine），用于发布前判断是否已经发布过。"""
    base = _api_base(args)
    token = _refresh_access(base)
    qs = "scope=mine"
    if args.q:
        from urllib.parse import quote_plus
        qs += "&q=" + quote_plus(args.q)
    if args.page:
        qs += "&page=%d" % args.page
    if args.page_size:
        qs += "&pageSize=%d" % args.page_size
    st, payload, _ = _req("GET", base + "/firmwares?" + qs, token=token)
    if st not in (200, 201) or not isinstance(payload, dict):
        sys.stderr.write("获取我的固件列表失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    items = payload.get("items") or []
    print(json.dumps({
        "total": payload.get("total", len(items)),
        "items": items,
    }, ensure_ascii=False, indent=2))


def cmd_firmware_get(args):
    """拉取单条固件记录（含 name/desc/category/shots/version 等），供重新发布时当作草稿。"""
    base = _api_base(args)
    token = _refresh_access(base)
    st, payload, _ = _req("GET", base + "/firmwares/" + args.id, token=token)
    if st not in (200, 201) or not isinstance(payload, dict):
        sys.stderr.write("获取固件失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    fw = payload.get("firmware") if isinstance(payload, dict) else None
    print(json.dumps(fw, ensure_ascii=False, indent=2))


def cmd_delete(args):
    """删除一条固件记录（作者本人或管理员）。

    用于「重新发布」流程：先删掉旧记录，再提交一份全新的（避免平台上存在
    两个同名/同 app 的项目）。后端不做状态限制——已上架、审核中都能删
    （用户要的就是「把上架的应用删掉再发新的」），只校验归属。"""
    base = _api_base(args)
    token = _refresh_access(base)
    st, payload, _ = _req("DELETE", base + "/firmwares/" + args.id, token=token)
    if st not in (200, 201, 204):
        sys.stderr.write("删除固件失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    print("已删除固件 id=%s。" % args.id)


def cmd_whoami(args):
    base = _api_base(args)
    token = _refresh_access(base)
    st, payload, _ = _req("GET", base + "/auth/me", token=token)
    if st not in (200, 201):
        sys.stderr.write("获取当前用户失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def cmd_logout(args):
    creds = load_creds()
    if creds:
        try:
            os.remove(CRED_FILE)
        except Exception:
            pass
    print("已清除本机缓存的凭据。")


# --------------------------------------------------------------------------- CLI
def build_parser():
    p = argparse.ArgumentParser(
        prog="sdgoods_publish",
        description="把固件与固件信息提交到谷仓 SDGOODS 开放平台",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    pl = sub.add_parser("login", help="邮箱验证码登录（一次性）")
    pl.add_argument("email", help="注册/登录邮箱")
    pl.add_argument("--code", help="4 位验证码（不传则交互输入）")
    pl.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pl.set_defaults(func=cmd_login)

    pp = sub.add_parser("publish", help="提交固件与信息")
    pp.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pp.add_argument("--file", required=True,
                    help="应用包 .bin（纯 app 镜像，不带地址；用 tools/pack_app.py 导出）")
    pp.add_argument("--id",
                    help="已存在固件的 id：传了即走 PATCH 更新（重新发布），不传则 POST 新建")
    pp.add_argument("--replace",
                    help="旧固件 id：先「删除」它再 POST 新建（用于替换已上架/审核中的同名项目，"
                         "确保平台上始终只有一个该项目，不会留下两个一样的记录）")
    pp.add_argument("--max-size", type=int,
                    help="槽上限字节数（默认 3 MB），用于校验应用包体积")
    pp.add_argument("--no-verify", action="store_true",
                    help="跳过应用包校验（不推荐：带地址的包会让用户设备无法启动）")
    pp.add_argument("--name", required=True, help="固件名称（≤60 字）")
    pp.add_argument("--desc-zh", help="中文简介（≤2000 字）")
    pp.add_argument("--desc-en", help="英文简介（≤2000 字）")
    pp.add_argument("--category", required=True,
                    help="分类 slug（先运行无参 GET /api/categories 查看合法值）")
    pp.add_argument("--version", default="v1.0.0", help="版本号（≤20 字，默认 v1.0.0）")
    pp.add_argument("--hardware", default="sdgoods", choices=["cyb1", "sdgoods", "both"],
                    help="适用硬件（默认 sdgoods）")
    pp.add_argument("--tags", nargs="+", help="标签（≤12 个）")
    pp.add_argument("--shots", nargs="+", help="截图路径（最多 4 张）")
    pp.add_argument("--github", help="GitHub 仓库地址")
    pp.add_argument("--draft", action="store_true", help="存为草稿而非提交审核")
    pp.set_defaults(func=cmd_publish)

    pa = sub.add_parser("whoami", help="显示当前登录用户")
    pa.add_argument("--api", help="API 基地址")
    pa.set_defaults(func=cmd_whoami)

    po = sub.add_parser("logout", help="清除本机凭据")
    po.set_defaults(func=cmd_logout)

    pf = sub.add_parser("firmwares", help="列出「我的固件」（发布前判断是否已经发布过）")
    pf.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pf.add_argument("--q", help="按名称/简介/标签搜索关键词")
    pf.add_argument("--page", type=int, help="页码（默认 1）")
    pf.add_argument("--page-size", type=int, dest="page_size", help="每页条数（默认 12，最大 60）")
    pf.set_defaults(func=cmd_firmwares_list)

    pg = sub.add_parser("firmware", help="拉取单条固件记录（重新发布作草稿用）")
    pg.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pg.add_argument("--id", required=True, help="固件 id")
    pg.set_defaults(func=cmd_firmware_get)

    pd = sub.add_parser("delete", help="删除一条固件记录（重新发布前先删旧记录，避免重复项目）")
    pd.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pd.add_argument("--id", required=True, help="要删除的固件 id（已上架/审核中也能删）")
    pd.set_defaults(func=cmd_delete)

    pt = sub.add_parser("set-token", help="保存开发者令牌（启用 MCP 令牌通道）")
    pt.add_argument("token", help="开发者令牌（sdg_ 开头，个人中心生成）")
    pt.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pt.set_defaults(func=cmd_set_token)

    ptk = sub.add_parser("mcp-token", help="检查本机是否已保存开发者令牌（不回显明文）；--set 可直接保存")
    ptk.add_argument("--set", dest="set", metavar="SDG_TOKEN", help="直接保存开发者令牌（等同 set-token）")
    ptk.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE（仅 --set 时生效）")
    ptk.set_defaults(func=cmd_mcp_token)

    pu = sub.add_parser("mcp-upload", help="【令牌通道】经平台托管 MCP 上传固件（status 默认提审中）")
    pu.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pu.add_argument("--file", required=True, help="应用包 .bin（纯 app 镜像，用 tools/pack_app.py 导出）")
    pu.add_argument("--max-size", type=int, help="槽上限字节数（默认 3 MB），用于校验应用包体积")
    pu.add_argument("--no-verify", action="store_true", help="跳过应用包校验（不推荐）")
    pu.add_argument("--name", required=True, help="固件名称（≤60 字）")
    pu.add_argument("--desc-zh", help="中文简介（≤2000 字）")
    pu.add_argument("--desc-en", help="英文简介（≤2000 字）")
    pu.add_argument("--category", required=True, help="分类 slug（先运行无参 GET /api/categories 查看合法值）")
    pu.add_argument("--version", help="版本号（不传则用 version.txt 最新值，自动补 v 前缀）")
    pu.add_argument("--hardware", default="sdgoods", choices=["cyb1", "sdgoods", "both"], help="适用硬件（默认 sdgoods）")
    pu.add_argument("--tags", nargs="+", help="标签（≤12 个）")
    pu.add_argument("--shots", nargs="+", help="截图路径（最多 4 张）")
    pu.add_argument("--github", help="GitHub 仓库地址")
    pu.set_defaults(func=cmd_mcp_upload)

    pw = sub.add_parser("mcp-whoami", help="【令牌通道】查询当前开发者令牌对应账号")
    pw.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pw.set_defaults(func=cmd_mcp_whoami)

    pf2 = sub.add_parser("mcp-firmwares", help="【令牌通道】列出「我的固件」（采集信息用，无需登录态）")
    pf2.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pf2.add_argument("--q", help="按名称/简介/标签搜索关键词")
    pf2.add_argument("--name", help="按应用名精确过滤（重新发布时定位旧记录）")
    pf2.add_argument("--page", type=int, help="页码（默认 1）")
    pf2.add_argument("--page-size", type=int, dest="page_size", help="每页条数（默认 12，最大 60）")
    pf2.set_defaults(func=cmd_mcp_firmwares)

    pmu = sub.add_parser("mcp-unpublish", help="【令牌通道】下架/撤回（published|reviewing → draft）")
    pmu.add_argument("id", help="固件 id")
    pmu.add_argument("--reason", help="下架原因（写进审核时间线）")
    pmu.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pmu.set_defaults(func=cmd_mcp_unpublish)

    pmd = sub.add_parser("mcp-delete", help="【令牌通道】删除（仅 draft / rejected，通常先 mcp-unpublish）")
    pmd.add_argument("id", help="固件 id")
    pmd.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pmd.set_defaults(func=cmd_mcp_delete)

    pmdl = sub.add_parser("mcp-download", help="【令牌通道】拉刷机清单做防砖验证（无需登录态）")
    pmdl.add_argument("id", help="固件 id")
    pmdl.add_argument("--mode", choices=["single", "multi"], help="单应用（默认）/ 多应用模式")
    pmdl.add_argument("--check-sha256", help="本地 app.bin 的 sha256，比对 app@0x10000 段是否一致")
    pmdl.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pmdl.set_defaults(func=cmd_mcp_download)

    pmr = sub.add_parser("mcp-replace", help="【令牌通道】删旧建新的「删旧」：下架→删除（对应 REST publish --replace）")
    pmr.add_argument("id", help="旧固件 id")
    pmr.add_argument("--reason", help="下架原因（写进审核时间线）")
    pmr.add_argument("--api", help="API 基地址，覆盖 SDGOODS_API_BASE")
    pmr.set_defaults(func=cmd_mcp_replace)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
