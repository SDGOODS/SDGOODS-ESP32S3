---
name: sdgoods-check-env
description: 检查「谷仓次元屏（SDGOODS-ESP32S3）」固件二次开发环境是否齐全（ESP-IDF / python3 / esptool / git / node+npm 字体链）。封装 tools/check_env.py，输出 JSON 供 AI 解析，逐项报出缺失项与安装指引。二次开发改代码前的第一道基础。Use when asked to verify / 检查 / 准备 the SDGOODS badge dev environment.
agent_created: true
---

# SDGOODS 开发环境检查

## 何时使用
- 用户要开始改固件 / 编译前，先确认环境齐全。
- 典型：克隆 SDGOODS-ESP32S3 后、或 CI 里预检。

## 运行（从仓库根目录）
```bash
python3 tools/check_env.py --json      # 给 AI/CI 解析：{"fail": N, "checks": [...]}
python3 tools/check_env.py             # 人类可读表格
```

## 解析结果
检查项分两级：
- **MUST**（缺了无法编译/烧录）：`python3`、`ESP-IDF(>=5.5)`、`esptool`
- **WARN**（仅特定需求）：`git`（克隆/提交）、`node/npm`（改/增中文文案才需，跑 `lv_font_conv`）
- **INFO**（状态提示）：是否已有编译产物、串口驱动平台

退出码：`0`=全部 MUST 齐全；`1`=有 MUST 缺失；`2`=参数错误。

## AI 处理策略
- `fail > 0`：先帮用户补齐 MUST 项，再进入编译。ESP-IDF 安装见
  https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/。
- 用户**只改中文文案**却报 node/npm 缺失：提示先
  `npm i lv_font_conv`（及首次 `python3 tools/fetch_fonts.py` 下载源字体），见 skill `sdgoods-fonts`。
- 按 `hint` 字段给平台相关指引，不要硬编不兼容平台的安装命令。
