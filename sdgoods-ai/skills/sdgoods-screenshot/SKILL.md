---
name: sdgoods-screenshot
description: ESP32「一键截屏」接收端，把谷仓次元屏经 USB 串口发的原始位图还原成 PNG，给 AI「看图」验证 UI/字体/画面。封装 tools/screenshot_recv.py（-t 自动触发、-p 端口、-n 张数、--caps 能力探测、--selftest 自检）。Use when asked to screenshot / 截屏 / capture the badge screen / verify UI visually.
agent_created: true
---

# SDGOODS 串口截屏

## 何时使用
- 验证 UI 改动、字体排版、画面是否正确（尤其手指够不到的启动台/菜单界面）。
- 单张约 2~5 秒（JPEG 编码）或约 25 秒（旧固件回退 RGB565）；比烧板子试错便宜得多。

## 运行（从仓库根目录）
```bash
# 自动向串口发 's' 触发并接收 1 张，存 shot_时间戳.png
python3 tools/screenshot_recv.py -t

# 指定端口 / 连续抓 3 张 / 指定输出
python3 tools/screenshot_recv.py -p /dev/cu.usbmodem21301 -n 3 -o my_shot.png

# 探测固件能力（串口发 '?'，看是否含 SHOT）
python3 tools/screenshot_recv.py --caps

# 不需要设备，自检解析 + PNG 写出
python3 tools/screenshot_recv.py --selftest
```
- 协议：`===SHOT-BEGIN w=360 h=360 ...===` 文本头 + 定长原始二进制 + `===SHOT-END===`；
  `fmt=1` 为 JPEG（PC 直接落 .jpg），`fmt=0`/无字段为 RGB565（PC 转 PNG）。
- 需要 pyserial：用带 pyserial 的解释器（系统 python3 可能无）。脚本会提示可用路径。

## AI 处理策略
- 接收前**先让用户关掉** `idf.py monitor` / 串口助手 / 浏览器 Web Serial 页面——
  它们占住同一串口会导致打不开或数据串台。
- 脚本保持 DTR/RTS 高电平避免打开瞬间复位，无需手动操作设备（`-t` 已自动触发）。
- 把生成的 PNG 用 Read 工具打开「看一眼」，核对布局/文字/颜色；发现方框即字体缺字（见 `sdgoods-fonts`）。
- 真机不在手边：用 `--selftest` 验证解析管线本身无问题，再请用户上机补真实截屏。

## 能力探测
- 网页端「从设备截图」会先发 `?` 探测 `SDGOODS-CAPS:SHOT`；本机 `--caps` 等价。
- 若缺 `SHOT`，提示用户在 BSP 启用 `CONFIG_SDGOODS_SCREENSHOT` 重编（见 docs/PUBLISHING.md）。
