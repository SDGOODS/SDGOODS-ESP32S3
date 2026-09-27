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

## 何时**不**需要重跑（省一次几十秒的转换）
`gen_fonts.py` 给两套字体都固定传了 `--range 0x20-0x7E`，**全部 ASCII（字母/数字/半角标点）
无条件包含**。所以只新增纯 ASCII 文案（如 `Slot 4 / 4`、`RAM 49 / 512 KB`、`Free 15.8 MB`、
`Data`、`Volume`）**不需要**重跑字体，直接编译即可 —— 别被「子集字体」四个字吓到，
先确认新字里有没有 CJK：没有就跳过本 skill。
（注意：`--check` 对 `si_yuan_black_*` 只按 `ICON_SYMBOLS` 精选集校验，不校验 ASCII，属正常。）

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

## 只给某处**换小一号字**？—— 别缩放，也别重跑整套
用户说「这几个数字/字母再小一点」时，两条**错路**先排除：

- ❌ **`lv_obj_set_style_transform_zoom()` 缩放**：LVGL 8.3 的位图变换走**最近邻**采样
  （`lv_draw_sw_transform`）⇒ 0.86 倍会把笔画抽成粗细不匀的断笔，真机看着就是「糊」。
  矢量图形可以 `LV_IMAGE`/arc 缩放，**位图字体不行**。
- ❌ **`gen_fonts.py --sizes 12`**：`--sizes` 确实可控，但它一次生成的是
  **`cn_font_12` + `si_yuan_black_icon_12` 这一对**（后者 `.fallback` 指向前者）⇒
  为了 3 位数字要多吃 ~100KB flash 的全量 CJK 子集，不划算。

✅ **正解：单独烘一套「字符集封闭」的小字号字体**（同一份源字体、同 bpp，风格一致）：

```bash
# 1) 装工具（⚠️ 别在已有 node_modules 的父目录里装：npm 会 ENOTEMPTY 挂在无关包上；
#    放到一个带独立 package.json 的空目录里再装）
mkdir -p /tmp/fontconv && cd /tmp/fontconv && printf '{"name":"fontconv","private":true}\n' > package.json
npm i lv_font_conv          # 1.5.3 与仓库既有字体同源代

# 2) 只烘需要的字符（--symbols 里写全：0-9、'-' 等）
node node_modules/lv_font_conv/lv_font_conv.js \
  --font <仓库>/tools/fonts/NotoSansSC-Regular.ttf \
  --size 12 --bpp 4 --format lvgl --no-compress --no-prefilter \
  --symbols "0123456789-" \
  --lv-font-name si_yuan_black_num_12 --lv-include lvgl.h -o /tmp/out.c
```
落地四步（缺一不可）：
1. 输出文件**手工加 OFL 头**（照抄 `si_yuan_black_icon_*.c` 的头，去掉 fallback 那段），
   并在头里写清「怎么重新生成」；
2. 放进 `components/sdgoods_board/fonts/`，**加进该目录的 `CMakeLists.txt` 字体列表**
   （那里是显式列举、不是 glob，漏了会 `undefined reference`）；
3. 用它的 .c 里 `LV_FONT_DECLARE(<name>);`；
4. **不挂 `.fallback`** —— 字符集是封闭的，缺字就是调用方写错了，不要静默回退成方框。

实例：首页电池框内百分比 `si_yuan_black_num_12`（源码 7.8KB / 编译后 ≈1.5KB，
把 14px 的 `100` 从「墨迹高 11 行、宽 17px」降到「9 行 / 13px」，正是 12/14 的比例）。

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
