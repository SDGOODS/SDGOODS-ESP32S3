# 单应用固件制作指南（SINGLE 模式）

> 适用：把一个 app **直接当作设备主机固件**刷进出厂区（factory），不装启动器、不装其它 app。
> 对照文档：`MULTI_APP_DYNAMIC_SLOTS.md`（多应用动态插槽）、`APP_SDK.md`（app 侧 SDK 与模式检测）。
> 2026-09-19 结论文档 —— 决策与验收都用真机跑过（见文末表格）。

---

## 1. 两种固件的关系

| | 多应用固件（MULTI） | 单应用固件（SINGLE） |
|---|---|---|
| 出厂区（factory）里放什么 | **启动器** `SDGOODS_LAUNCHER` | **你的 app** |
| app 从哪启动 | 启动器把 app.bin 装进 `ota_N`，`esp_ota_set_boot_partition` 切过去 | 直接从 factory 启动 |
| 谁管 otadata | **只有启动器** | 你的 app 自己（标准 A/B 自更新） |
| 分区表 | 平台 `partitions.csv` | **建议原样沿用**（见 §2） |
| 设备模式字符串 | `MULTI` | `SINGLE` |
| 控制中心第 5 个按钮 | `Exit`（返回启动器） | `Power`（关机） |

模式是**本固件自己判出来的**，不需要平台配合，口径见 `APP_SDK.md` §3.3。

---

## 2. ★ 决策：单应用固件**沿用平台分区表**

只把**出厂区的内容**换成你的 app，`partitions.csv` 原样不动：

```bash
# 1) 编出你的 app（本示例即 SDGOODS-ESP32S3，产物 SDGOODS_EBADGE.bin）
idf.py -B build_app0 build

# 2) 把纯应用镜像写进出厂区（0x10000）
esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX \
    write_flash 0x10000 build_app0/SDGOODS_EBADGE.bin

# 3) 清掉选槽记录 ⇒ 下次从 factory 启动（否则会先从上次的 ota_N 启动）
esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX erase_region 0x310000 0x2000
```

**为什么不要为省 flash 另出一张精简分区表**：

1. **有地方放持久数据**：`appdata`(16MB, data/fat) 在。否则 app 只能退到几百 KB 的 NVS，
   而 `APP_SDK.md` §4 的兜底路线会从「异常分支」变成「常态」。
2. **能做标准 A/B 自更新**：`otadata` + `ota_0..3` 都在，`esp_https_ota()` /
   `esp_ota_get_next_update_partition()` 开箱可用。
3. **将来要转多应用只烧出厂区**：不用重刷分区表、不用搬家数据 —— 直接换成
   `write_flash 0x10000 <启动器>.bin` 即可，插槽与 appdata 都原地保留。

> ⚠️ 若确实要出精简分区表（例如把 16MB appdata 省掉）：
> **必须保留 `nvs`**（WiFi 凭据、i18n、平台设置都靠它），`otadata` + `ota_N` 若要自更新也必须留。
> 此时 `appdata` 缺失是合法状态 —— 但 app 必须有 §4 的 NVS 兜底分支，否则「存不上」会变成静默 bug。

> ⚠️ 无论走哪种分区表，**都要保留工程里的这两行 sdkconfig**
> （`CONFIG_FATFS_LFN_HEAP=y` / `CONFIG_FATFS_MAX_LFN=64`，见 `BUILD.md` §2.1）：
> `appdata/<app_id>/` 的目录名是 `project_name`，必然超过 FAT 的 8.3 短名限制。
> 少了它，`mkdir` 返 `EINVAL(22)`，数据静默落空 —— 而短名字（≤8 字符）却看着正常。

---

## 3. 命名坑（会直接改变行为，不是风格问题）

**`project_name` 不能叫 `SDGOODS_LAUNCHER`。**

模式判定的第二问是「出厂区里这份固件是不是启动器」，依据就是 `esp_app_desc_t.project_name`。
单应用固件若取这个名字，会被判成**启动器宿主** ⇒ `MODE MULTI` + 控制中心第 5 个按钮显示 `Power`
（看着没错），但它其实管不着任何槽，`Slot n / N` 之类的多应用语义会全是假数据。

正确做法：`CMakeLists.txt` 里 `project(<你的工程名>)`，与你在平台上注册的工程名一致
（它同时也是 `appdata/<app_id>/` 的目录名）。

---

## 4. 单应用固件里**不能**调的 API

平台层 2026-09-19 加了权限闸门（`APP_SDK.md` §3.4）：被拒时返回
`ESP_ERR_INVALID_STATE` 并打一条 `refused` 日志，**零副作用**。单应用固件会撞上这些：

| API | 单应用固件调用结果 |
|---|---|
| `sdgoods_return_to_launcher()` | 拒绝（本固件不是被管理的 app）—— 单应用没有「启动器」可回 |
| `sdgoods_launcher_launch_slot(idx)` | 拒绝（同上；`idx>=0` 另需宿主身份） |
| `sdgoods_launcher_boot_check()` | 拒绝。⚠️ **别调**：它的下游 `orphan_appdata_cleanup()` 判据是「appdata 目录名能否对上某个已装槽」，单应用设备上**没有任何已装槽** ⇒ 所有目录算孤儿 ⇒ **每次开机把 app 自己的数据删光** |
| `sdgoods_launcher_self_check()` / `_orphan_appdata_cleanup()` | 同上，拒绝 |
| `install` / `uninstall` / `find_free_slot` | 拒绝（仅宿主可调）/ 只读查询无害 |

这些是**宿主（启动器）的职责**。app 侧只要不调，就什么都不会发生。

---

## 5. 自更新（SINGLE 下归你自己的事）

单应用固件在 MULTI 下不能碰 otadata（那是启动器的簿记对象）；**SINGLE 下正相反** ——
整块 flash 归你，标准 A/B 自更新直接用：

```c
/* 标准 IDF 流程：写 ota_0 → set_boot_partition → restart */
const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
/* ...把新固件写进 next，再 esp_ota_set_boot_partition(next)，重启... */
```

自更新后运行分区变成 `ota_0`，出厂区里仍是你的旧版本。**这会被判成 SINGLE 吗？**
会 —— 判定第二问是「**出厂区里装的是不是启动器**」，你的固件不是启动器 ⇒ 仍是 `SINGLE`
（2026-09-19 修复前这里会误判成 MULTI，导致控制中心第 5 个按钮变成 `Exit`，
一点就 `set_boot_partition(factory)` **回滚到旧版本**。详见 `APP_SDK.md` §3.3 与真机用例）。

⚠️ 想「回滚到出厂版本」是另一回事：那是**替换出厂区内容**，属于重新烧录，不是 OTA。

---

## 6. 验收清单

刷完板子上电，逐项核对（串口日志 + 控制中心）：

| 检查项 | 期望 |
|---|---|
| 启动日志 | `running 'launcher' (subtype=0x00, factory), app='<你的工程名>' (not the launcher) -> SINGLE` |
| 控制中心底部小字 | `Mode SINGLE` |
| 控制中心第 5 个按钮 | `Power`（不是 `Exit`） |
| 数据页第一行 | `App <你的工程名>`（不是 `Slot 0 / 4`） |
| 持久化后端 | `appdata partition present=yes` → `appdata ready for '<工程名>': /appdata/<工程名>` → `backend=appdata … boots=N`；重启后 `boots` +1 |
| 串口发 `r` | `return-to-launcher refused … mode=SINGLE`，**不重启** |
| 串口发 `L` | `launch_slot refused … mode=SINGLE`，**不重启** |
| 重启后 | 数据页仍 `App <你的工程名>`，且 `Slot` 不会再出现 |
| **自更新之后** | 以上全部**仍然成立**（模式不许变 MULTI） |

> 顺带：控制中心数据页在 SINGLE 下的第一行之所以改成 `App ...`，就是因为
> `Slot 已装/总槽` 在单应用设备上是假数据（4 个 ota 槽存在、一个 app 都没装）。

---

## 7. 从一个 app 工程做出两种固件

同一份源码既能上架（MULTI）也能当主机固件（SINGLE），**不需要条件编译**：
「我是谁 / 我在哪有意义」全部由 `sdgoods_device_mode()` / `sdgoods_device_is_managed_app()`
在运行时回答。所以：

- 上架路径：编译 → 平台安装通道写进 `ota_N` → 模式自动 `MULTI`
- 单应用路径：编译 → 写 `0x10000`（factory）+ 擦 otadata → 模式自动 `SINGLE`

两条路共用同一份 `app.bin`（不带地址的纯应用镜像），不需要分别打包。
