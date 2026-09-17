---
name: sdgoods-fonts
description: 谷仓次元屏中文子集字体：新增/修改中文文案后必须重跑生成，否则屏上显示方框(tofu)。封装 tools/gen_fonts.py（扫描源码字符集、生成 cn_font_*/si_yuan_black_icon_* 两套 OFL 字体）与 tools/font_metrics.py（离线核对圆屏文本宽度与缺字，避免烧板试错）。Use when Chinese text changes / 字体 / 方框 / 缺字 / 超宽 / tofu in the badge UI.
agent_created: true
---

# SDGOODS 中文子集字体

## 关键事实
- 固件 flash 仅几 MB，塞不下整套中文字库（20MB+），所以用**子集字体**：只扫描源码里实际出现的字符烘进 .c。
- 两套字体：`cn_font_14/16`（全量子集，兜底）；`si_yuan_black_icon_14/16`（UI 精选子集，主字体，`.fallback` 编译期指向 `cn_font_*`）。
- 界面用 `si_yuan_black_icon_*`；精选集外的字由 LVGL 自动回退 `cn_font_*`。
- 源字体 Noto Sans SC（SIL OFL 1.1，允许嵌入再分发含商用）。

## 何时必须重跑
**任何新增/修改中文文案后** —— 否则新字在屏上是方框。改完文案 → 生成 → 编译 → 截屏确认（skill `sdgoods-screenshot`）。

## 运行（从仓库根目录）
```bash
# 首次：下载源字体（需联网）
python3 tools/fetch_fonts.py

# 装字体转换工具（node >= 16）
npm i lv_font_conv

# 重新生成全部字体（14/16 号）
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv

# 只校验当前字体是否缺字，不重新生成（很快）
python3 tools/gen_fonts.py --check
```
- 依赖 node/npm + lv_font_conv；未装时 `check_env.py` 会报 WARN。

## 离线度量（圆形屏专用）
圆屏「可用宽度」不是常数：同一行越靠上/下越窄（弦宽 `2*sqrt(180²-dy²)`）。先量再烧：
```bash
python3 tools/font_metrics.py                 # 用内置关键文案表
python3 tools/font_metrics.py --strings "你的新文案" --size 14
```
- 退出码 `0`=通过；`1`=缺字或超宽。
- 缺字 → 重跑 `gen_fonts.py`；超宽 → 缩小字号/缩短文案/限宽折行（`lv_label_set_long_mode WRAP`）。
- 圆屏整行文本带 y 坐标会自动按弦宽收紧上限；写死单一 340px 会在边缘位置误判「OK」。

## 坑
- 生成文件头带 OFL 版权声明，**不得删**。
- `cn_font_*` / `si_yuan_black_icon_*` 前缀的文件被 `gen_fonts.py` 跳过扫描（避免字符集只增不减），不要手改这些文件。
- CJK 长串（无空格）LVGL 不会自动断行；度量工具宁严勿松，真超了会报超宽让你改。
