# 平台托管的预编译引导层（本地调试用）

本目录下的文件是**平台（谷仓 SDGOODS 开放平台）统一下发**的引导层，
二次开发者**不需要、也不应该**自行编译或修改它们：

| 文件 | 落点 | 说明 |
|------|------|------|
| `bootloader.bin` | `0x0` | 官方二级引导程序，由 IDF + 平台 sdkconfig 编译 |
| `partition-table.bin` | `0x8000` | 多应用分区表，**必须与 `../partitions.csv` 完全一致** |

## 为什么放在这里

- **平台安装时自动下发**：开发者把 app 提交到开放平台后，平台会按 `app_desc`
  自动把官方引导层 + 你的 app 拼成完整固件烧录，开发者侧的引导层不会被使用。
- **仅供本地调试**：在没有平台、只想用 USB 串口把 app 烧进开发板自测时，
  才需要这两个预编译件——它们随仓库下发，省去开发者自己编译 bootloader 的麻烦。

## 重要约束

- **不要修改这两个文件**，也不要提交对它们的改动。分区表若与平台不一致，
  会导致 app 槽位偏移、OTA 失败等难以排查的问题。
- 分区表内容由 `../partitions.csv` 决定；平台侧改了分区，这里要同步替换
  （由仓库维护者用平台官方构建产物覆盖，非开发者职责）。
- 这两个 `.bin` 已被 `.gitignore` 的例外规则 `!platform/prebuilt/*.bin` 强制纳入版本库，
  确保 clone 下来就能直接用于本地调试。

## 配合脚本

本地一键烧录见 `tools/flash_local.sh`：

```bash
python3 tools/flash_local.sh               # 自动找 /dev/cu.usb* 或 /dev/ttyUSB*，烧录默认构建
python3 tools/flash_local.sh -p /dev/cu.usbmodem1234 -b build_pub
```
