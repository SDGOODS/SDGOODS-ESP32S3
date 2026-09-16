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
配合谷仓次元屏（SDGOODS-ESP32S3）固件开发使用：

    # 1) 一次性登录（邮箱收 4 位验证码，refreshToken 会缓存在本机）
    python3 tools/sdgoods_publish.py login you@example.com

    # 2) 提交固件（截图可选，最多 4 张）
    python3 tools/sdgoods_publish.py publish \
        --file build/SDGOODS_EBADGE.bin \
        --name "我的固件" \
        --desc-zh "一句话介绍这个固件" \
        --desc-en "One-line intro" \
        --category game \
        --shots shot1.png shot2.png \
        --github https://github.com/you/your-fw

API 基地址：环境变量 SDGOODS_API_BASE（与网页端约定一致），或 --api 参数。
例如生产环境：  export SDGOODS_API_BASE=https://你的平台域名/api

想完全不用本工具、让 AI 直接调接口？见 docs/PUBLISHING.md 的 curl 示例，
本文件就是那套 REST API 的忠实复刻（鉴权→presign→PUT→POST）。
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


def _valid_categories(base):
    st, payload, _ = _req("GET", base + "/categories")
    if st not in (200, 201) or not isinstance(payload, dict):
        return None  # 拿不到也不阻塞，交给服务端校验
    cats = payload.get("categories") or []
    return {c.get("slug") for c in cats if isinstance(c, dict)}


def cmd_publish(args):
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

    # 固件 bin
    if not args.file or not os.path.isfile(args.file):
        sys.stderr.write("请通过 --file 指定固件 .bin 路径。\n")
        sys.exit(1)
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

    st, payload, _ = _req("POST", base + "/firmwares", token=token, body=body)
    if st not in (200, 201):
        sys.stderr.write("提交固件失败（HTTP %s）：%s\n" % (st, _err_msg(payload)))
        sys.exit(1)
    fw = (payload.get("firmware") if isinstance(payload, dict) else None) or payload
    fid = (fw.get("id") if isinstance(fw, dict) else None) or "?"
    print("提交成功！固件 id=%s（状态：%s）。" % (fid, body["status"]))
    print("可在「我的固件」里查看、补充信息或提交审核。")


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
    pp.add_argument("--file", required=True, help="固件 .bin 路径")
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
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
