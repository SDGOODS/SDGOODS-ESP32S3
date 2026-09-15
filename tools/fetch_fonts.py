#!/usr/bin/env python3
"""下载源字体（SIL OFL 1.1 的 Noto Sans SC）到 tools/fonts/。

为什么需要这一步
----------------
仓库里**不存放**源字体（完整的简体中文 TTF 有 10MB，会让 clone 变慢），
只存放由它生成的**子集字体** —— components/sdgoods_board/fonts/*.c，几百 KB。
要重新生成字体（改了中文文案之后必须做）时，先跑本脚本把源字体拉下来。

下载源按顺序尝试，任一成功即止：
  1. fonts.gstatic.com        Google Fonts 官方静态分发
  2. cdn.jsdelivr.net         noto-cjk 仓库的 CDN 镜像
  3. raw.githubusercontent    noto-cjk 原始文件（国内常超时）

也可以自己在别处下载后放到 tools/fonts/NotoSansSC-Regular.ttf。

用法
----
    python3 tools/fetch_fonts.py                 # 下载到 tools/fonts/
    python3 tools/fetch_fonts.py --force         # 已存在也重新下载
    python3 tools/fetch_fonts.py --out /tmp/x.ttf
"""

import argparse
import os
import struct
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "tools", "fonts", "NotoSansSC-Regular.ttf")

SOURCES = [
    ("Google Fonts (fonts.gstatic.com)",
     "https://fonts.gstatic.com/s/notosanssc/v36/"
     "k3kCo84MPvpLmixcA63oeAL7Iqp5IZJF9bmaG9_FnYxNbPzS5HE.ttf"),
    ("jsDelivr (noto-cjk 镜像)",
     "https://cdn.jsdelivr.net/gh/notofonts/noto-cjk@main/"
     "Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf"),
    ("raw.githubusercontent (noto-cjk)",
     "https://raw.githubusercontent.com/notofonts/noto-cjk/main/"
     "Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf"),
]


def ttf_name(path, want_id):
    """读 TrueType name 表里的指定字段（1=家族名, 13=许可声明）。"""
    with open(path, "rb") as f:
        d = f.read(4096 * 64)
        n = struct.unpack(">H", d[4:6])[0]
        tables = {}
        for i in range(n):
            o = 12 + i * 16
            tag = d[o:o + 4].decode("latin1")
            off, ln = struct.unpack(">II", d[o + 8:o + 16])
            tables[tag] = off
        if "name" not in tables:
            return ""
        no = tables["name"]
        fmt, cnt, so = struct.unpack(">HHH", d[no:no + 6])
        for i in range(cnt):
            pid, eid, lid, nid, ln, off = struct.unpack(">HHHHHH", d[no + 6 + i * 12:no + 6 + i * 12 + 12])
            if nid == want_id and lid in (0x409, 0):
                s = d[no + so + off:no + so + off + ln]
                try:
                    return s.decode("utf-16-be").strip()
                except Exception:
                    continue
    return ""


def download(url, out, label):
    print(f"  尝试 {label} ...")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (font fetch)"})
        with urllib.request.urlopen(req, timeout=180) as r, open(out + ".part", "wb") as f:
            total = int(r.headers.get("Content-Length") or 0)
            got = 0
            while True:
                chunk = r.read(1 << 16)
                if not chunk:
                    break
                f.write(chunk)
                got += len(chunk)
                if total and got % (1 << 22) < (1 << 16):
                    print(f"    {got/1048576:.1f}/{total/1048576:.1f} MB", end="\r")
        sz = os.path.getsize(out + ".part")
        if sz < 1024 * 1024:
            print(f"    ✗ 文件只有 {sz} 字节，不像是完整字体，放弃这个源")
            os.remove(out + ".part")
            return False
        os.replace(out + ".part", out)
        print(f"    ✓ {sz/1048576:.1f} MB")
        return True
    except Exception as e:
        print(f"    ✗ {type(e).__name__}: {e}")
        if os.path.exists(out + ".part"):
            os.remove(out + ".part")
        return False


def main():
    ap = argparse.ArgumentParser(description="下载源字体 Noto Sans SC (SIL OFL 1.1)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="保存路径")
    ap.add_argument("--force", action="store_true", help="已存在也重新下载")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    if os.path.isfile(out) and not args.force:
        print(f"已存在，跳过下载: {out}")
        print(f"  （要重新下载请加 --force）")
    else:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        print("下载 Noto Sans SC（约 10MB）...")
        ok = False
        for label, url in SOURCES:
            if download(url, out, label):
                ok = True
                break
        if not ok:
            sys.exit(
                "\n所有下载源都失败了。请手动下载 Noto Sans SC 并保存为：\n"
                f"  {out}\n"
                "任一来源均可：\n"
                "  · https://fonts.google.com/noto/specimen/Noto+Sans+SC\n"
                "  · https://github.com/notofonts/noto-cjk/releases\n")

    # 校验：是合法 TTF + 家族名 + 许可
    if open(out, "rb").read(4) not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
        sys.exit(f"✗ 不是合法的 TTF/OTF 文件: {out}")
    fam = ttf_name(out, 1)
    lic = ttf_name(out, 13)
    print(f"\n✓ 字体信息")
    print(f"  家族名 : {fam}")
    print(f"  许可   : {lic[:110] or '(name 表无许可字段)'}")
    if "Noto Sans" not in fam and "Source Han" not in fam:
        print("  ⚠️ 家族名不含 Noto Sans / Source Han —— 请确认该字体的再分发授权")
    else:
        print("  ✅ SIL OFL 1.1：允许嵌入到软件中再分发（含商用），"
              "需保留版权声明、不得单独售卖字体文件")


if __name__ == "__main__":
    main()
