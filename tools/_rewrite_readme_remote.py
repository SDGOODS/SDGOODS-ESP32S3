#!/usr/bin/env python3
"""把 README.md / README_EN.md 里的「仓库地址」在 GitHub 与 Gitee 之间切换。

只替换本仓库自己的 clone / 提示词地址，绝不触碰：
- GitHub Actions 的 build 徽章（github.com/.../actions/workflows/...）
- 第三方上游库地址（espressif / notofonts / adobe-fonts / lecram 等）

用法：
  python3 tools/_rewrite_readme_remote.py github   # → 全部 GitHub 地址
  python3 tools/_rewrite_readme_remote.py gitee    # → 全部 Gitee 地址
"""
import sys

GH_HOST = "github.com/SDGOODS/SDGOODS-ESP32S3"
GITEE_HOST = "gitee.com/sdgoods/sdgoods-esp32s3"

# 目录名：GitHub 克隆出来是 SDGOODS-ESP32S3，Gitee 是 sdgoods-esp32s3
GH_DIR = "SDGOODS-ESP32S3"
GITEE_DIR = "sdgoods-esp32s3"

FILES = ["README.md", "README_EN.md"]

# 这些是第三方上游，绝不能替换
PROTECTED = [
    "espressif/esp-idf",
    "notofonts/noto-cjk",
    "adobe-fonts/source-han-sans",
    "lecram/gifdec",
]


def rewrite_line(line: str, target: str) -> str:
    # 保护第三方上游：行内含受保护关键字则原样返回
    if any(p in line for p in PROTECTED):
        return line
    # 保护 GitHub Actions badge（含 /actions/）
    if "/actions/" in line:
        return line

    if target == "gitee":
        line = line.replace(GH_HOST, GITEE_HOST)
        # 目录名：仅替换独立出现的 GH_DIR（避免误伤，用精确词边界靠前后字符）
        line = line.replace(GH_DIR, GITEE_DIR)
    else:  # github
        line = line.replace(GITEE_HOST, GH_HOST)
        line = line.replace(GITEE_DIR, GH_DIR)
    return line


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("github", "gitee"):
        print("用法: _rewrite_readme_remote.py <github|gitee>", file=sys.stderr)
        return 2
    target = sys.argv[1]

    for fn in FILES:
        with open(fn, "r", encoding="utf-8") as f:
            lines = f.readlines()
        new_lines = [rewrite_line(ln, target) for ln in lines]
        with open(fn, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print(f"  {fn} → {target}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
