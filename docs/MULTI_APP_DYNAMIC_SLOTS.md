# 动态插槽（固定分区表 + 动态指派）端到端设计

> 状态：设计稿 **v0.3**（2026-09-19 修订 —— 决策 #6 改为「首启进启动器」、端点表按实际实现更正、
> 新增 §11 落地进度表；全部 9 项决策仍有效，进入分层落地阶段）
> 范围：设备 launcher 槽管理 + 平台设备槽状态/安装编排 + 端云协议，三端一次性出总览，再逐层细化。
> 决策依据：本节是「多 app 共存架构（MEMORY §6）」的落地形态之一。已与用户拍板两件事：
> 1. **形态 = 固定分区表 + 动态指派**：`partitions.csv` 编译期预置 N 个 `ota_N` 槽，但「哪个 app 进哪个槽、安装/卸载/切换/回退」运行时由 launcher + 平台动态决定，**不重写分区表**。
> 2. **本轮出端到端总设计**（本文件）。

---

## 0. 目标 / 非目标

**目标**
- 设备支持多个第三方 app 共存；槽位数量在编译期固定，但槽 ↔ app 的映射在运行时动态。
- 平台按设备记录「槽占用 / 已装 app / 状态」，提供设备视角的应用管理能力。
- 安装 / 卸载 / 切换对开发者透明：开发者只交一个不带地址的 `build/<项目>.bin`（见 `docs/BUILD.md` §4）。

**非目标（本稿明确排除）**
- ❌ 运行时改写分区表（MEMORY §6「装 app = 追加槽项」方案已否定：写坏即砖、且与 IDF 槽编号连续性铁律冲突）。
- ❌ launcher 内嵌式插件热加载：app 是完整固件镜像（IROM/DROM 虚拟地址），切换 = 重启，无法像 `.so` 那样动态加载。脚本轻应用（MEMORY §11）走 Lua 通道，不占 ota 槽。
- ❌ 本稿不含 launcher 的具体 UI / 渲染实现，只定「槽管理 + 协议 + 状态」。

---

## 1. 分区布局（固定表，动态指派）

沿用 MEMORY §6 的布局，把「ota_0..3 共 4 槽」作为固定默认（**已拍板：4 槽 × 3MB**，详见 §9.1）。数量由 `CONFIG_SDGOODS_APP_SLOTS` 控制，本设计锁 `4`。

```
# Name,      Type, SubType,  Offset,    Size,     Flags
nvs,         data, nvs,      0x9000,    0x6000,
phy_init,    data, phy,      0xF000,    0x1000,
launcher,    app,  factory,  0x10000,   0x300000,   # 启动器（多应用宿主 / 菜单 / OS），3MB
otadata,     data, ota,      0x310000,  0x2000,     # 选槽记录（IDF 原生）
ota_0,       app,  ota_0,    0x320000,  0x300000,   # app 槽 ×4，各 3MB
ota_1,       app,  ota_1,    0x620000,  0x300000,
ota_2,       app,  ota_2,    0x920000,  0x300000,
ota_3,       app,  ota_3,    0xC20000,  0x300000,
appdata,     data, fat,      0x1000000, 0x1000000,  # 高 16MB（16MB→32MB），app 持久数据，按 app_id 隔离
```

**铁律（来自 MEMORY §6/§7）**
- `ota_N` 必须从 `ota_0` **连续编号**，中间不能有洞；否则 IDF `esp_ota_get_app_partition_count()` 遇洞即停、bootloader 按编号索引 → 后面的槽永远不可达。
- `launcher` 占 `factory@0x10000`（与现网旧表逐字节一致，迁移安全，见 §8）。
- app 镜像存虚拟地址（IROM `0x42000000` / DROM `0x3C000000`），bootloader 经 MMU 动态映射 → **同一个 app.bin 可写任意 64KB 对齐槽位**，落点由平台按内容识别（MEMORY §7 三路判据）。
- 引导层（`<0x10000`：bootloader / 分区表 / launcher / otadata）只有官方能下发（MEMORY §7 两道闸门），开发者 app 只进 `ota_N`。

**槽数 / 槽大小权衡**：低 16MB 内留给槽约 13.1MB。4×3MB=12MB（留余量）；若 `CONFIG_SDGOODS_APP_SLOTS=8` 则每槽须缩到 ~1.6MB（app 体积上限随之下降）。默认 **4 槽 × 3MB**。

**app 数据落 16MB 之后的硬约束**（用户 2026-09-18 明确）：`appdata` 分区位于 `0x1000000`（16MB）起的高 16MB（16MB→32MB）。所有 app 的持久用户数据（用户生成内容、GIF/字体缓存等）一律只写这块区域；**平台刷机（`SystemAsset` 合并 + app 落点）只写低 16MB（launcher + `ota_N`），绝不触碰高 16MB 的 appdata**。这与 MEMORY §6「平台只能写低 16MB」一致。实现守卫：平台 `/download` 合并 parts 时，任何地址 `>= 0x1000000` 的段必须拒绝（沿用 `asset_layout_mismatch` 思路，另加 `appdata_protected` 守卫），防止把引导层/app 误写到用户数据区；反过来，appdata 的写入只发生在设备上（launcher 运行时挂载 `fat`/LittleFS），不在刷机链路里——这也意味着**卸载清 appdata 是设备侧动作（§6），平台刷机不会动它**。

---

## 2. 设备端槽清单（slot manifest，设备的真相源）

平台的「设备槽状态」是缓存；**设备的 flash 才是最终真相**。设备维护一份持久化的槽清单，开机自校验。

**存储位置**（二选一，推荐 NVS）：
- NVS namespace `sdgoods_slots`，每槽一个 key `slot_<i>`；或
- 独立小分区 `slotmeta`（如 `app, slotmeta, 0x... , 0x1000`）。

**单槽记录结构（伪代码）**
```c
typedef struct {
  uint8_t  slot_idx;          // 0..K-1
  char     app_id[40];        // 平台 Firmware.id（空串 = 空槽）
  char     package_id[40];    // 同 app_id，便于审计
  char     version[24];       // 与 Firmware.version 对齐
  uint8_t  sha256[32];        // 期望 sha256，写入后校验用
  uint8_t  state;             // 0=empty 1=installed 2=corrupted 3=updating
  uint32_t last_launch;       // 最近一次启动时间戳
} slot_entry_t;
```

**自校验**：每次开机，launcher 读每槽 `esp_app_desc_t`（位于槽起始 `+0x20`，magic `0xABCD5432`）：
- 读不到 magic / version 不符 manifest → 标 `corrupted`，菜单里置灰或隐藏；
- 校验通过 → 标 `installed`。
- manifest 与 flash `desc` 冲突时，**以 flash 为准**（manifest 只是缓存），并触发 `POST /slots/sync` 让平台对账（§5）。

**appdata 孤儿清理（开机自动，用户 2026-09-18 明确）**
- 每次启动（launcher 起来后），扫描高 16M 的 `appdata/`（见 §1 硬约束）：枚举其下每个 `appdata/<app_id>/` 子目录。
- 对每个子目录，检查该 `app_id` 在槽清单里是否仍有 `state==installed` 且 `desc` 校验通过的槽：
  - **有**对应已装 app → 保留（含「槽损坏但 app 仍在、准备重装恢复」的情况，appdata 留着以便重装复用）。
  - **无**（app 被删除 / 槽已空 / manifest 里查无此 app_id）→ **自动 `rm -rf appdata/<app_id>/`**，回收高 16M 空间。
- 这是 §6「卸载即清 appdata」之外的**兜底**：覆盖「app 槽被擦/被外部刷掉、但没走正常卸载流程」等 orphan 场景，保证高 16M 不留死数据。
- 该清理只在设备上发生（高 16M 平台刷机不碰，见 §1），与平台状态无关、不依赖网络。

**与 otadata 的分工**
- `otadata`：决定「下次启动**哪个**槽」（IDF 原生，`esp_ota_set_boot_partition` 写）。
- `manifest`：决定「每个槽里**是什么** app、什么版本」。
- 两者独立：换槽（切换 app）只动 otadata；装/卸 app 才动 manifest + 擦写 flash。

---

## 3. 启动 / 切换 / 回退流程

**上电**
1. bootloader 读 `otadata` → 指向 `ota_N` 且镜像完好 → 直接跑 app；指向 `factory` → 跑 launcher。
2. 若 `ota_N` 镜像损坏 → IDF bootloader **自动回落 factory（launcher）**，天然救援通道（MEMORY §6）。
3. launcher 起来后：先做 §2 的槽自校验，再跑 **appdata 孤儿清理**（开机自动回收「已删 app」的高 16M 数据，见 §2）。

**launcher 菜单**
- 读 manifest，列出 `installed` 的 app（并实时校验 `desc` 完好；损坏的标红/隐藏）。
- 用户选 app → `esp_ota_set_boot_partition(ota_N)` + `esp_restart()` → 重启进该 app。

**app 内返回 launcher（关键约定）**
- app 是完整固件，重启后由 bootloader 直接进 `ota_N`，**不会自动回 launcher**。
- 因此**所有上架到 launcher 的 app 必须提供「返回启动器」入口**，统一调用：
  ```c
  // sdgoods_app_sdk（提供给开发者的极薄库）
  void sdgoods_return_to_launcher(void) {
      const esp_partition_t *f = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
      esp_ota_set_boot_partition(f);
      esp_restart();
  }
  ```
- 第一方 app 内置；第三方 app 在 `docs/APP_SDK.md` 里作为上架硬性要求写明（含最小可上架示例）。

**回退（app 异常）**
- app 崩溃/掉电后下次启动仍会进该 `ota_N`（otadata 未改）。这是「回到上次用的 app」，属预期。
- 若想永远先回 launcher，可在 app 启动时先 `esp_ota_set_boot_partition(factory)`，但会多一次重启；默认不这么做。

---

## 4. 平台侧数据模型（新增 Device + DeviceAppSlot）

现状（已核实 `schema.prisma`）：有 `User / ApiToken / Firmware / FirmwarePart / SystemAsset`，**无 Device 模型、无设备鉴权**。`AssetKind` 已预留 `LAUNCHER` / `OTADATA`（注释「尚未启用」），本设计把这两格用起来。

**新增 `Device`**
```prisma
model Device {
  id        String   @id @default(cuid())
  ownerId   String
  owner     User     @relation(fields: [ownerId], references: [id], onDelete: Cascade)
  name      String
  hardware  Hardware @default(SDGOODS)
  tokens    DeviceToken[]              // 已拍板（§9.1 #4）：新增 DeviceToken，与用户 ApiToken 隔离
  lastSeen  DateTime?
  createdAt DateTime @default(now())
  slots     DeviceAppSlot[]
}

// 已拍板（§9.1 #4）：设备凭证独立成表，与用户 ApiToken **完全隔离**；
// launcher 持明文 token，平台只存 hash；scope 限定在设备侧操作。
model DeviceToken {
  id        String   @id @default(cuid())
  deviceId  String
  device    Device   @relation(fields: [deviceId], references: [id], onDelete: Cascade)
  tokenHash String   @unique              // bcrypt/sha256(token)，下发时只给一次明文
  scopes    String   @default("device:read device:install device:slots device:sync")
  createdAt DateTime @default(now())
  lastSeen  DateTime?
  revokedAt DateTime?                    // 撤销即失效，不影响该设备其它 token
  @@index([deviceId])
}
```

**新增 `DeviceAppSlot`（平台的设备槽真相缓存）**
```prisma
model DeviceAppSlot {
  id         String   @id @default(cuid())
  deviceId   String
  device     Device   @relation(fields: [deviceId], references: [id], onDelete: Cascade)
  slotIndex  Int                         // 0..K-1
  firmwareId String                      // 指向 Firmware.id（app 包）
  firmware   Firmware @relation(fields: [firmwareId], references: [id])
  version    String
  status     SlotStatus @default(QUEUED) // QUEUED/INSTALLING/INSTALLED/FAILED/UNINSTALLING
  installedAt   DateTime?
  lastLaunchedAt DateTime?
  @@unique([deviceId, slotIndex])
  @@index([deviceId, status])
}

enum SlotStatus { QUEUED INSTALLING INSTALLED FAILED UNINSTALLING }
```

**`Firmware` 侧已拍板加 `kind` 字段（§9.1 #7）**
```prisma
enum FirmwareKind { APP LAUNCHER LIGHTAPP }

model Firmware {
  // ... 既有字段（owner / name / status / parts ...）...
  kind      FirmwareKind @default(APP)
}
```
- `APP`：第三方/第一方应用包（单 app.bin，落 `ota_N`）。本设计的「可安装 app」= `kind=APP` + `status=PUBLISHED` + 通过单 app.bin 校验（MEMORY §7）。
- `LAUNCHER`：启动器镜像（落 `0x10000`，等价于官方装机包的 launcher 段）；经 `SystemAsset(LAUNCHER)` 或装机包下发，开发者不可提交。
- `LIGHTAPP`：脚本轻应用（MEMORY §11 的 Lua 通道），不占 ota 槽，走另一安装通道，本设计不展开。
- `AssetKind`（平台既有，已预留 `LAUNCHER`/`OTADATA`）与 `FirmwareKind` 语义对齐，避免两套枚举漂移；`/install` 与可安装目录只列 `kind=APP`。

---

## 5. 端云协议（launcher ↔ 平台，设备拉模型）

采用**设备拉（pull）模型**：launcher 主动调用平台，平台不下推。理由：ESP32 多在 NAT/移动网络后，无稳定入站通道；拉模型天然穿透 NAT，且平台只需「目录 + 下载 + 记录」。

**设备鉴权**：launcher 持 **`DeviceToken`**（已拍板 §9.1 #4，与用户 `ApiToken` 完全隔离的独立凭证）对应的明文 token，所有 `/devices/me/*` 用 `Authorization: Bearer <deviceToken>`；平台按 `tokenHash` 查 `DeviceToken` 表，校验 `scopes`（须含 `device:install`/`device:slots`/`device:sync` 等），`revokedAt` 非空即拒。一个设备可有多枚 `DeviceToken`（如换机/重置后重发），互不干扰。

**端点**
| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/devices/me/apps` | 可安装目录（含每个 app 当前在各设备槽的安装态） |
| GET | `/api/devices/me/slots` | 平台视角的本机槽清单（与设备 flash 对账用） |
| POST | `/api/devices/me/install` | 请求装某 app；body `{ firmwareId, slotIndex? }`；返回 `{ slotIndex, downloadUrl(presign), sha256, sizeBytes }`；平台写 `DeviceAppSlot(INSTALLING)` |
| POST | `/api/devices/me/uninstall` | body `{ slotIndex }`；平台标 `UNINSTALLING`；设备擦槽后发 sync |
| POST | `/api/devices/me/sync` | body = 设备真实 manifest（`{ slotCount, slots[] }`）；平台以设备为准对账，校正状态 |
| POST | `/api/devices/me/slots/{idx}/launch` | 仅上报 `lastLaunchedAt`（切换本身是设备本地 set_boot+restart，无需平台往返） |

> ⚠️ **端点口径修正（2026-09-19）**：实现里 **uninstall / sync 不带 `{idx}`**（`/devices/me/uninstall`
> 收 `body.slotIndex`、`/devices/me/sync` 收整份 manifest），只有 `launch` 是 `{idx}` 形式。
> 本表已按**实际实现**更正 —— 以 `server/src/routes/devices.ts` 为准
> （`POST /devices/me/uninstall` @:380、`POST /devices/me/sync` @:399、`POST /devices/me/slots/:idx/launch` @:459）。
> 原先「槽内二级路径」的写法只是设计初稿，曾被照抄进实现文档，容易让设备侧写错 URL。

**槽分配归谁**？推荐**平台在 install 响应里下发 slotIndex**（平台是 fleet 真相源，避免多设备/多次请求抢同一槽）。设备按指引写入；若本地 flash 实际与平台不符（如之前手动刷过），以设备 flash 为准并在 sync 里纠正平台。备选：设备自选空闲槽再上报（更简单但 fleet 视图需靠 sync 收敛）。

---

## 6. 安装 / 卸载 / 切换 时序

**安装**
```
launcher ── POST /install {firmwareId} ──▶ 平台
平台：挑空闲槽(DeviceAppSlot 无该 slotIndex 记录) → 记 INSTALLING
        ◀── {slotIndex, downloadUrl, sha256}
launcher：下载 app.bin → 写 ota_{slotIndex}
        ：校验 esp_app_desc_t + sha256 一致
        ：更新本地 manifest(state=installed)
        ── POST /sync {manifest} ──▶ 平台：状态 INSTALLED，记 version
```

**卸载**（已拍板：卸载直接清除 app 数据，见 §9.1 #3）
```
launcher ── POST /uninstall {slotIndex} ──▶ 平台：标 UNINSTALLING
launcher：擦 ota_{idx}
        ：删除 appdata/<app_id>/ 隔离目录（整目录清空，不留用户数据）
        ：清 manifest(state=empty)
        ── POST /sync ──▶ 平台：删/置空该 DeviceAppSlot
```
- `appdata/<app_id>/` 用 app 的 `app_id` 做目录名隔离；卸载即 `rm -rf` 该目录。隐私默认：不保留、不询问。

**切换（无需平台往返）**
```
用户选 app → launcher 本地 esp_ota_set_boot_partition(ota_N)+restart
        ──（可选）POST /slots/{idx}/launch 仅上报 lastLaunchedAt ──▶ 平台
```

---

## 7. 边界与安全

- **槽满**：平台 `/install` 找不到空闲槽 → 返回「槽已满」4xx；launcher 接到后弹窗**让用户选择删除哪个 app**（删除走 §6 卸载流程），不自动替出（见 §9.1 #5）。平台侧不替用户决定替出对象。
- **app 体积 > 槽**：上传期 `pack_app.py` 已限 3MB（`DEFAULT_SLOT_BYTES=3*1024*1024`）；槽 3MB，已拍板安全线 **app ≤ 2.9MB**（留镜像头/段余量），超过直接拒收（见 §9.1 #2）。
- **安装中断（掉电）**：manifest 留 `updating`；下次开机检测 `desc` 无效 → 标 `corrupted`、不列出；用户可重试安装覆盖该槽。
- **清单与 flash 不一致**：以 flash 的 `desc` 为准，sync 时以设备上报纠正平台。
- **权限**：app 只能进 `ota_N`（>0x10000）；引导层（launcher/bootloader/分区表/otadata）只经 `SystemAsset`（LAUNCHER/BOOTLOADER/PARTITION_TABLE/OTADATA）或官方装机包下发，开发者 app 永不触碰（沿用 MEMORY §7 两道闸门）。launcher 本身经 `SystemAsset(LAUNCHER)` 托管 + 装机包迁移（§8）。
- **回退救援**：任何 `ota_N` 损坏 → bootloader 自动回 factory（launcher），不会砖。

---

## 8. 从单应用设备迁移

沿用 MEMORY §6 的次序（bootloader 不动，只写两段）：
1. 写 `otadata`（空白 8KB，旧表不认这块）；
2. 写**新分区表**（factory 起始仍 `0x10000` ⇒ 旧固件照常启动；表内含 `ota_0..3` 槽）；
3. 写 launcher 到 `0x10000`（覆盖旧固件，完成后即被旧表当 factory 启动）；
4. 写**预装示例 app**（`Hello SDGOODS`）进 `ota_0`（**已拍板预装**，见 §9.1 #6）；**`otadata` 保持空白/不动**，
   首启进**启动器**（2026-09-19 修订，理由见 §9.1 #6）。其余槽 `ota_1..3` 空置。其余 app 经 §5 协议按需安装。
   - 预装 **不需要**额外写 manifest：启动器 `self_check()` 开机逐槽读 `esp_app_desc_t`，magic 有效即**收编**为
     `INSTALLED`（`slot_manifest.c:185-227`）⇒ 裸刷进槽的 app 自然出现在主页网格里。

**刷机时显示已安装 app 信息**（见 §9.1 #8）
- 平台「刷机 / 设备管理」页在发起刷机或展示设备时，**拉取 `DeviceAppSlot`（或设备 sync 的 manifest）并列出已装 app**：名称、版本、所在槽位、状态（installed/updating/corrupted）、最近启动时间。
- 设备侧 launcher 的安装/切换界面同样基于 manifest 列出已装 app，与平台视图同源。
- 目的：让用户刷机/管理前先看到「这机器上现在有哪些 app、占哪个槽」，避免误覆盖；槽满时此列表即「让用户选择删除哪个」的弹窗数据源。

无 launcher 的旧设备：保持单应用，或走「装机包」迁移。迁移后 app 经 §5 协议进槽。

---

## 9. 决策状态（已拍板 → 待拍板）

### 9.1 已拍板（2026-09-18 第二轮对齐）

| # | 议题 | 决策 |
|---|---|---|
| 1 | 槽数 `CONFIG_SDGOODS_APP_SLOTS` | **4 槽 × 3MB**。固定分区表预置 4 个 `ota_N`，不动态增减；「动态」只在槽↔app 映射层（运行时指派）。 |
| 3 | 卸载是否清 `appdata` | **直接清除**：卸载 = 擦 `ota_{idx}` + 删除 `appdata/<app_id>/` 隔离目录，不留用户数据（隐私默认，不询问）。 |
| 5 | 槽满策略 | **不自动替出**：平台 `/install` 无空闲槽时返回「槽已满」，launcher 弹窗**让用户选择删除哪个 app**（走 §6 卸载）后再装；平台不替用户决定替出对象。 |
| 6 | 装机预装示例 app | **预装 `Hello SDGOODS` 进 `ota_0`；首启进启动器**（2026-09-19 修订）。原文写「`otadata` 指向 `ota_0` 使其首启即进该 app」，实际实现与本修订一致 —— `flash.sh` 只写 `ota_0@0x320000`、**不写 `otadata`**。修订理由：① 首启要让用户看到**宿主 + 已装 app 列表**（正是 #8 那张表）；② app 的「返回启动器」入口是**第三方 app 的义务**，漏做就把用户锁死在 app 里（`otadata` 是唯一选槽记录，断电也回不去）；③ 启动器 `self_check()` 会自动收编裸刷的 app，预装照样可见 ⇒ **零行为变更，只改口径**。 |
| 8 | 刷机时显示已安装 app 信息 | **平台刷机/设备管理页须展示已装 app 列表**（来自 `DeviceAppSlot` 或设备 sync 的 manifest）：名称、版本、槽位、状态、最近启动时间；launcher 安装/切换界面同源列出已装 app（§8 末段）。 |
| 9 | 开机孤儿 appdata 清理 | launcher **每次启动**扫描高 16M 的 `appdata/`，凡 `app_id` 在槽清单里已无 `installed` 槽（app 被删）的目录自动 `rm -rf`；是 §6 卸载清数据的兜底，覆盖「app 槽被外部擦掉没走正常卸载」的 orphan 场景（§2）。 |
| 2 | app 安全体积线 | **≤2.9MB**（已拍板）；`pack_app.py` 槽上限 3MB，留镜像头/段余量，超过拒收（§7）。 |
| 4 | 设备鉴权方案 | **新增 `DeviceToken`**：与用户 `ApiToken` 完全隔离的独立凭证表（`tokenHash` + `scopes` + `revokedAt`），launcher 调 `/devices/me/*` 用其 Bearer token（§4 `DeviceToken` 模型 + §5）。 |
| 7 | `Firmware.kind` 字段 | **新增** `enum FirmwareKind { APP LAUNCHER LIGHTAPP }` + `Firmware.kind`：区分应用包/启动器/脚本轻应用；`/install` 与可安装目录只列 `APP`（§4）。 |

### 9.2 全部决策已拍板

上述 #1–#9 均已定，无剩余待拍板项。下一步进入分层落地（建议顺序见 MEMORY §12 / 设计稿后续章节）。

---

## 10. 与既有约定的关系

- 复用 MEMORY §6 布局、§7 三路落点判据 + 引导层两道闸门、§8 `SystemAsset` 刷机合并。
- 复用 `pack_app.py` 的 3MB 槽上限与五项判据（开发者只交单 app.bin）。
- 脚本轻应用（MEMORY §11）走 Lua 通道 + `appdata` 分区，**不占 ota 槽**，与本设计正交。

---

## 11. 落地进度表（2026-09-19 盘点）

> 真相源：设备侧 `SDGOODS_LAUNCHER`（平台层经同步脚本下发到 `SDGOODS-ESP32S3` / `SDGOODS-HELLO`）；
> 平台侧：谷仓 SDGOODS 开放平台（服务端，不开源）。图例：✅ 代码在且已真机/接口验证 · ⚠️ 有代码但行为与设计不符 · ❌ 未实现。
> 详细缺口清单（含 file:line 与可复现命令）见会话根目录 `MULTI_SLOT_REMAINING_WORK.md`。

| # | 环节 | 状态 | 证据 / 缺口 |
|---|---|---|---|
| 1 | 分区表 4 槽 + appdata | ✅ | `partitions.csv`：`otadata@0x310000`、`ota_0..3@0x320000+`、`appdata@0x1000000` |
| 2 | 槽数配置 | ✅ | `Kconfig`（`CONFIG_SDGOODS_APP_SLOTS`）；运行时以分区表为准（`slot_count()`） |
| 3 | 槽清单 manifest + 开机自检 | ✅ | `slot_manifest.c:185 self_check()`，裸刷 app 自动收编 |
| 4 | 孤儿 appdata 清理 | ✅ | `slot_manifest.c:262 orphan_appdata_cleanup()` |
| 5 | 安装 / 卸载实现 | ⚠️ | 见 P2#11：`install()` 用 `OTA_SIZE_UNKNOWN` ⇒ **整槽 3MB 全擦** + 要求整包驻留内存 |
| 6 | 权限闸门（宿主 / 被管 app） | ✅ | `deny_unless_host()` @`slot_manifest.c:172`，零副作用 + 模式前缀日志 |
| 7 | 设备模式判定 SINGLE/MULTI | ✅ | `device_mode.c` 两问判据，见 `APP_SDK.md` §3.3 |
| 8 | app 侧 SDK（appdata 优先 + NVS 兜底） | ✅ | `sdgoods_appdata_begin()` @`app_sdk.c` |
| 9 | **设备侧槽管理 UI**（安装/卸载/槽满弹窗） | ❌ | `install`/`uninstall`/`find_free_slot` **调用者 = 0**；无管理页、长按菜单、进度 UI |
| 10 | **设备侧网络 / 配网 / DeviceToken** | ❌ | main+board+launcher 三处 `esp_wifi`/`esp_http_client`/`esp_tls` **零引用** |
| 11 | 救援通道（坏 app 回厂） | ❌ | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 未开；仅 ota_N 镜像损坏才回 factory |
| 12 | 平台数据模型 | ✅ | `Device`/`DeviceAppSlot`/`DeviceToken`/`FirmwareKind` + migration `20260918042506_add_device_models` |
| 13 | 平台设备 API `/devices/me/*` | ✅ | `devices.ts`：含 SINGLE `409`、2.9MB 拒收、槽满、scope 校验、presign 下载 |
| 14 | 平台 SystemAsset（LAUNCHER@0x10000） | ✅ | `systemAssets.ts` + `admin/system-assets.html` |
| 15 | **发 / 撤 DeviceToken 的界面** | ✅ | 个人中心「我的设备」tab：`my.html#panel-devices` + `store.js devices.*`（开户/补发/吊销/切模式/删设备 + **已装应用表**，即决策 #8）。真浏览器验证 `scripts/e2e-my-devices-ui.js` 26/26 |
| 16 | **刷机页展示已装 app + 覆盖护栏** | ✅ | `esp-flasher.js inspect()` 读设备真实分区表/出厂区/各槽，`evaluateFlashedTargets()` 纯函数决策（`scripts/test-flash-targets.mjs` 35 断言 + 真机三路径）；`market.html guardFlashTargets()` 在写入前拦截；`store.js` 透出 `skipped` |
| 17 | `/ota/manifest` 按 `kind` 过滤 | ✅ | `ota.ts` `where.kind = 'APP'`；真机库验证：插入 `LAUNCHER` fixture 后旧代码泄漏且被推成 `latest`，修后清单 15→14 且 `latest` 回到应用 |
| 18 | 市场固件的 `Firmware.kind` 写入口径 | ✅ | **决定：市场固件恒为 `APP`，不提供 kind 选择器。** `Firmware.kind` 全库只有 4 处读取且都是 `kind: 'APP'` 过滤，无任何写入点；启动器走**另一张表** `SystemAsset(kind='LAUNCHER')`（已在 #14 落地，有独立的 `assertAssetContent` 与地址治理）。给市场后台加 kind 下拉=凭空造一个没有消费者的开关，还会把「引导层只经 SystemAsset / 官方装机包下发」这条线拆开。`LIGHTAPP` 等 Lua 轻应用通道落地时再回来加 |
| 19 | 落地进度表 | ✅ | 本节 |

**缺口归因**：1–8、12–17 是「槽的骨架」与**端云协议的上半段**（设备开户、令牌、目录、槽清单、落点护栏）；
剩下的断点是 **9/10 —— 设备侧自己那一半**：徽章既没有槽管理界面，也还没有任何联网代码，
所以现在的多插槽只能靠 `esptool` 手刷使用，平台侧的设备/令牌只是「先建好档案」。
- 平台 `AssetKind.LAUNCHER/OTADATA` 已预留并落地（#14）。
- 落地顺序建议：#9（本地槽管理，不依赖网络，用串口注入 bin 调试）→ #11（救援通道，与 #9 同批，
  否则界面一挂上就可能把自己锁死）→ #10（配网 + DeviceToken + 拉取安装，前置是先设计绑定流程）。
- ⚠️ #5 的「整槽 3MB 全擦 + 整包驻留内存」必须在 #9 之前修掉，否则管理页一挂上就卡住好几秒。
