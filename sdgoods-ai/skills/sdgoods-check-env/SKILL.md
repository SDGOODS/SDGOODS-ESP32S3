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
python3 tools/check_env.py             # 人类可读 + 分步进度（推荐给人类看）
python3 tools/check_env.py --no-progress   # 只给结论，不打分步进度
```
⚠️ 脚本已改为**延迟执行 + 分步打印**（`plan` 里存的是函数、不是调用结果）：
每检查一项先打 `[k/7] xxx ...`、再打 `[OK] [k/7] xxx <详情>`。
`--json` 时进度走 **stderr**，stdout 仍是纯 JSON（不会被进度污染）。

## ⏱ 进度播报规范（AI 必读：不要让界面长时间无输出）

**① 跑检查前先报一行计划**，用户才知道要等多久：
> 正在检查开发环境（共 7 项，通常 3–10 秒；idf.py / esptool 冷启动各可能到 20 秒上限）。

- 想让用户/自己看到分步进度 → 用**不带 `--json`** 的人类模式（进度在 stdout）。
- 只想拿结论给代码解析 → `--json`（进度在 stderr，不影响解析；AI 想看就 `2>&1`）。
- 检查本身**不要**放后台跑 —— 它最多几十秒，同步跑完直接把结果贴给用户更清楚。

**② 需要补装环境时，按阶段播报并给预估**（装全量约 1.2 GB，是国内用户最耗时的一环，
静默跑会被当成卡死）。每阶段开始前都要先说一句，不要等跑完再一起说：

| 阶段 | 动作 | 预估 | 播报文案 |
|---|---|---|---|
| ① | `jihu-mirror.sh set` 切极狐镜像 | <5s | `[1/5] 切换乐鑫国内镜像…` |
| ② | `git clone --recursive` 拉 ESP-IDF | 3–15 分钟（~491MB） | `[2/5] 拉取 ESP-IDF 源码（国内镜像，约 500MB，请稍候）…` |
| ③ | `install.sh esp32s3` 下工具链 | 5–20 分钟（~495MB） | `[3/5] 下载工具链（约 500MB，最慢的一步）…` |
| ④ | Python 依赖（install.sh 内自动跑） | 1–3 分钟（~212MB） | `[4/5] 安装 Python 依赖…` |
| ⑤ | 解压 LVGL 离线包（24MB） | <10s | `[5/5] 放置 LVGL 离线包…` |

**③ 长阶段（>60 秒）一律后台执行**，并遵守：
- 启动后立刻告知用户「已开始，跑完我会回报」；
- 期间不要反复轮询刷屏，也不要一句不说；
- 每完成一个阶段回一条 `[k/5] ✅ xxx 完成（用时 mm:ss）`，让用户看到进度在动。

**④ 结束后给一张汇总**（哪几项 MUST 缺、补齐命令、下一步是什么），不要只给一句「环境有问题」。

## 解析结果
检查项分两级：
- **MUST**（缺了无法编译/烧录）：`python3`、`ESP-IDF(>=5.5)`、`esptool`
- **WARN**（仅特定需求）：`git`（克隆/提交）、`node/npm`（改/增中文文案才需，跑 `lv_font_conv`）
- **INFO**（状态提示）：是否已有编译产物、串口驱动平台

退出码：`0`=全部 MUST 齐全；`1`=有 MUST 缺失；`2`=参数错误。

## 国内网络加速（装 ESP-IDF 前先做，实测结论）
装环境要下约 **1.2 GB**：SDK 源码 ~491MB + 工具链 ~495MB + Python 包 ~212MB。
工具链**绝大部分走 GitHub Releases**（`github.com/espressif/*/releases/download/...`），
国内直连常几 KB/s 或断流，是整个环境最耗时的一环。

```bash
export IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets   # 效果最大，实测 200
export PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
npm config set registry https://registry.npmmirror.com    # 只有改中文文案才需
./install.sh esp32s3          # 带 target，省掉用不到的 285MB riscv32 工具链
```

🔴 **两条实测红线**（写错会全部下不来）：
1. `IDF_GITHUB_ASSETS` 的值**不能带 `://`**，否则 `idf_tools.py` 直接 `fatal` 退出；
   尾部斜杠无所谓（脚本会 rstrip）。
2. **不要**用 `IDF_MIRROR_PREFIX_MAP` 把 `dl.espressif.com` 映射到 `dl.espressif.cn`——
   国内站**只有 `github_assets`**，`/dl/...` 全部 404（实测 cmake、xtensa 包均 404）。

其他离线手段：
- **工具链预置**：`~/.espressif/dist/` 放**同名**压缩包，idf_tools.py 检测到即打印
  `already downloaded` 跳过下载（idf_tools.py ≈1199 行）。适合放自建下载站/内网。
- **SDK 源码（首选）**：用乐鑫官方 `esp-gitee-tools` 的 `./jihu-mirror.sh set`
  （把 GitHub URL 改写到极狐镜像 `jihulab.com/esp-mirror`），之后**照常 clone GitHub 地址**，
  主仓 + 23 个子模块全走国内；工具链用官方工具里的 `$EGT_PATH/install.sh esp32s3`
  （不要用 IDF 自带的 install.sh）。
  ⚠️ tag 是 **`v5.5`** 不是 `v5.5.0`——v5.5 首发标签就叫 v5.5，之后才是 v5.5.1。
- **LVGL 组件（98MB）**：`python3 tools/pack_lvgl_offline.py` 打成
  `dist/lvgl__lvgl_8.3.11.tar.gz`（实测 1357 文件 / 24.0MB），用户解压到工程根目录即可；
  补丁在 `main/patches/` 随源码走，configure **无条件覆盖**，带不带补丁都不影响。

🔴 **不要自建 Gitee 镜像仓库**：Gitee 社区版单仓库 ≤500MB、单文件 ≤50MB、附件 ≤100MB，
  免费版**无 LFS**；工具链单包 272MB / 160MB 直接超限。乐鑫官方 `esp-gitee-tools` 文档原话：
  「没有镜像到 gitee 的原因为 gitee 非企业账户不支持占用空间大的仓库」——官方镜像已迁极狐。
  离线分发请选对象存储 + CDN，不要用 Gitee。

完整说明见仓库 `docs/ENVIRONMENT.md` §2.5。

## AI 处理策略
- `fail > 0`：先帮用户补齐 MUST 项，再进入编译。ESP-IDF 安装见
  https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/。
- **国内用户**：先按上面的加速段配好镜像再装，别直接 `./install.sh` 硬下 GitHub。
- 用户**只改中文文案**却报 node/npm 缺失：提示先
  `npm i lv_font_conv`（及首次 `python3 tools/fetch_fonts.py` 下载源字体），见 skill `sdgoods-fonts`。
- 按 `hint` 字段给平台相关指引，不要硬编不兼容平台的安装命令。
