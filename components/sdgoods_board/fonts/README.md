# 字体说明（components/sdgoods_board/fonts/）

本目录的 `.c` 文件是**子集字体**（扫描工程源码字符集后只取用到的字形，
用 [lv_font_conv](https://github.com/lvgl/lv_font_conv) 生成，非完整字库）：

| 文件 | 上游字体 | 许可 |
|---|---|---|
| `cn_font_14.c` / `cn_font_16.c` | [Noto Sans SC](https://github.com/notofonts/noto-cjk) | SIL OFL 1.1 |
| `si_yuan_black_icon_14.c` / `si_yuan_black_icon_16.c` | [Source Han Sans（思源黑体）](https://github.com/adobe-fonts/source-han-sans) | SIL OFL 1.1 |

完整许可文本见同目录 [`OFL.txt`](OFL.txt)，根目录 [`NOTICE`](../../../NOTICE) 有逐项归属声明。

再分发须知：

- 这些 `.c` 是上游 OFL 字体的**衍生**，整体沿用 SIL OFL 1.1，不受本仓库
  Apache-2.0 约束（两许可可共存于同一分发物）。
- 保留各 `.c` 文件头部的版权与许可声明，随分发带上 `OFL.txt`。
- 衍生字体的**主字体名**不得使用上游保留字体名（'Source' / 'Noto'）。

新增中文文案后如何重生成：见 [`tools/gen_fonts.py`](../../../tools/gen_fonts.py)
与根 README「你的第一个应用，已经会这些」一节的字体提示。
