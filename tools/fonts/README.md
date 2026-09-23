# tools/fonts/ —— 子集字体的上游源文件（按需下载，不入库）

本目录存放生成子集字体所需的**上游字体源文件**（`.ttf` / `.otf`），
体积较大（约 10MB/个），**已被 `.gitignore` 排除、不随仓库分发**。
需要重新生成字体子集时，先运行：

```bash
python3 tools/fetch_fonts.py   # 从上游官方渠道下载
python3 tools/gen_fonts.py     # 扫描工程字符集，生成 components/sdgoods_board/fonts/*.c
```

## 源文件与许可

| 源文件 | 上游项目 | 许可 |
|---|---|---|
| `NotoSansSC-Regular.ttf` | [Noto Sans SC](https://github.com/notofonts/noto-cjk) | SIL OFL 1.1 |
| （如需图标字重）Source Han Sans | [adobe-fonts/source-han-sans](https://github.com/adobe-fonts/source-han-sans) | SIL OFL 1.1 |

两个上游项目均以 SIL Open Font License 1.1 授权：源文件可自由下载、使用、
再分发（不得单独出售字体文件本身）。生成的子集衍生字体沿用 OFL 1.1，
完整许可文本见
[`components/sdgoods_board/fonts/OFL.txt`](../components/sdgoods_board/fonts/OFL.txt)。
