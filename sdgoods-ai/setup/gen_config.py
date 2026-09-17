#!/usr/bin/env python3
# ============================================================================
# SDGOODS AI Toolkit · MCP 配置生成器 (Phase 4)
# ----------------------------------------------------------------------------
# 读 sdgoods-ai/catalog.json，按平台输出对应的 MCP 配置 JSON 与安装命令。
# 用途：
#   1) 本地：被 gen_setup.py 调用，生成 setup/index.html 里「复制 MCP 配置」所需内容。
#   2) 开放平台后端：一键「复制 MCP 配置」按钮直接调用本脚本，拿到与前端一致的配置。
#
# 用法：
#   python3 sdgoods-ai/setup/gen_config.py --platform=claude
#   python3 sdgoods-ai/setup/gen_config.py --platform=workbuddy --server=/abs/path/server.py --json
#   python3 sdgoods-ai/setup/gen_config.py --platform=all --install
#
# 安全：本脚本只输出公开基地址与路径，绝不输出任何密钥/token。
# ============================================================================
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG = os.path.join(os.path.dirname(HERE), "catalog.json")
SERVER_DEFAULT = os.path.normpath(
    os.path.join(os.path.dirname(HERE), "mcp", "sdgoods-mcp-server", "server.py")
)


def load_catalog():
    with open(CATALOG, "r", encoding="utf-8") as f:
        return json.load(f)


def render_mcp_config(catalog, platform, server_path):
    """返回该平台的 MCP 配置 dict（已替换 __SERVER_PATH__）。"""
    tpl = catalog["mcp_config_templates"].get(platform)
    if tpl is None:
        raise SystemExit("未知平台: %s（可选: %s）" % (platform, ", ".join(catalog["mcp_config_templates"].keys())))
    raw = json.dumps(tpl, ensure_ascii=False)
    raw = raw.replace("__SERVER_PATH__", server_path)
    return json.loads(raw)


def platform_block(catalog, p, server_path):
    """返回单个平台的安装 + MCP 配置信息。"""
    install_cmd = p.get("install_cmd_mcp", p.get("install_cmd", ""))
    cfg = render_mcp_config(catalog, p["id"], server_path)
    return {
        "id": p["id"],
        "name_zh": p.get("name_zh", p["id"]),
        "name_en": p.get("name_en", p["id"]),
        "install_cmd": install_cmd,
        "skill_dir": p.get("skill_dir", ""),
        "agent_form": p.get("agent_form", ""),
        "mcp_location": p.get("mcp_location", ""),
        "mcp_config": cfg,
        "mcp_config_json": json.dumps(cfg, ensure_ascii=False, indent=2),
    }


def main():
    ap = argparse.ArgumentParser(description="SDGOODS AI Toolkit MCP 配置生成器")
    ap.add_argument("--platform", required=True,
                    help="workbuddy | claude | cursor | all")
    ap.add_argument("--server", default=SERVER_DEFAULT,
                    help="server.py 绝对路径（默认仓库内相对路径）")
    ap.add_argument("--json", action="store_true",
                    help="输出机器可读 JSON（含每个平台的 install + mcp_config）")
    ap.add_argument("--install", action="store_true",
                    help="额外打印安装命令（人类可读）")
    args = ap.parse_args()

    catalog = load_catalog()
    server_path = os.path.abspath(args.server)

    if args.platform == "all":
        platforms = catalog["platforms"]
    else:
        hit = [p for p in catalog["platforms"] if p["id"] == args.platform]
        if not hit:
            raise SystemExit("未知平台: %s（可选: %s）" % (
                args.platform, ", ".join(p["id"] for p in catalog["platforms"]) + ", all"))
        platforms = hit

    if args.json:
        out = {
            "server_path": server_path,
            "api_base": catalog.get("api_base"),
            "platforms": [platform_block(catalog, p, server_path) for p in platforms],
        }
        print(json.dumps(out, ensure_ascii=False, indent=2))
        return

    for p in platforms:
        blk = platform_block(catalog, p, server_path)
        if args.install:
            print("# ---- %s (%s) ----" % (blk["name_zh"], blk["name_en"]))
            print("# 安装：%s" % blk["install_cmd"])
            print("# 技能落点：%s" % blk["skill_dir"])
            print("# Agent 形态：%s" % blk["agent_form"])
            print("# MCP 落点：%s" % blk["mcp_location"])
        print(json.dumps(blk["mcp_config"], ensure_ascii=False, indent=2))
        print()


if __name__ == "__main__":
    main()
