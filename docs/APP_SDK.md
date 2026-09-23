# 应用端侧 SDK 接入指南（返回启动器 + appdata）

> 适用：所有要上架到「谷仓 SDGOODS 开放平台」、跑在谷仓次元屏（ESP32-S3）上的应用。
> 配套：动态插槽设计 `docs/MULTI_APP_DYNAMIC_SLOTS.md`（§3「app 内返回 launcher」、§1「appdata 高 16M 硬约束」）；
> 提交固件到平台见 `docs/PUBLISHING.md`。
> 平台层（BSP / 应用 SDK）以 Apache-2.0 发布，可自由商用、可闭源，只需保留声明 + NOTICE。

本文件只讲一件事：**写出一个能正常上架、能正常返回启动器、数据不越界的应用，要接哪些端侧 API。**

---

## 0. 两种运行模式（先看清你属于哪种）

用户在安装应用时可二选一，两种模式走完全不同的链路：

| 模式 | 含义 | 设备侧行为 | 返回启动器怎么做 |
|---|---|---|---|
| **单应用模式**（SINGLE） | 设备只跑一个 app，等价于「直接刷这个 app」 | app 作为**主机固件**运行，启动器不介入 | 用 `sdgoods_app_shell` 自带的「退出」即可回到应用页（不重启） |
| **多应用模式**（MULTI） | 设备同时装多个 app，动态插槽管理 | 每个 app 是**独立固件镜像**，烧进 `ota_N` 槽，由启动器（launcher）拉起 | **必须**调用 `sdgoods_return_to_launcher()`（设启动槽为 factory + 重启）回到启动器 |

> ⚠️ 关键点：单应用模式下，app 就是**主机固件**（启动器不介入）；多应用模式下，app 是**独立固件**，重启后由 bootloader 直接进 `ota_N`，**不会自动回启动器**。因此本指南的「返回启动器」硬性要求，主要针对**多应用模式**的上架 app。若你的 app 两种模式都要支持，统一用 `sdgoods_return_to_launcher()` 最省事。
>
> ⚠️ 单应用固件若要**自己更新**（标准 A/B OTA），更新完会运行在 `ota_N` 上 —— 这台设备
> 仍然是 **SINGLE**，别把这种情况当成多应用模式（见 §3.3 口径 2）。前提是**别往 factory
> 里刷启动器**：出厂区里装的必须是这个 app 自己。

无论哪种模式，你都只交一个**不带地址的 `build/<项目>.bin`**（见 `docs/BUILD.md` §4 与 `tools/pack_app.py`）。平台按内容识别它是 app 包，再决定烧进 `ota_N` 还是直接刷。

---

## 1. 应用 SDK 一览（`#include "sdgoods_app_sdk.h"`）

平台层 `components/sdgoods_launcher` 暴露给 app 的极薄库：

```c
/* ---- 返回启动器 / 持久数据 ---------------------------------------------- */
/* 返回启动器（factory）并重启。多应用模式 app 的「返回」按钮统一调它。
   ★ 带**模式闸门**（2026-09-19 起）：只有「被启动器管理的 app」会真的重启；
     单应用固件 / 启动器宿主调用会返回 ESP_ERR_INVALID_STATE 且**不重启**（只打 ERROR 日志）。
     返回值从 void 改为 esp_err_t 是向后兼容的 —— 忽略返回值的老代码照常编译。 */
esp_err_t sdgoods_return_to_launcher(void);

/* ★ 推荐用这个拿私有数据目录：挂载 appdata → 建好 /appdata/<app_id>/ → 返回路径。
   失败（含「本固件没有 appdata 分区」= ESP_ERR_NOT_FOUND）时 out 不可用，
   调用方**必须退 NVS** —— 详见 §4。 */
esp_err_t sdgoods_appdata_begin(const char *app_id, char *out, size_t out_len);

/* 本固件的分区表里有没有 appdata(data/fat) 分区。只查分区表、不挂载，随时可调。 */
bool sdgoods_appdata_available(void);

/* 挂载 appdata 分区（高 16M 的 data/fat，幂等：重复调用安全）。返回 ESP_OK 表示可用。 */
esp_err_t sdgoods_appdata_mount(void);

/* 卸载 appdata 分区。⚠️ 控制中心数据页会临时卸载，再次读文件前重新 mount 一次即可。 */
void sdgoods_appdata_unmount(void);

/* 只拼路径、不挂载也不建目录，如 /appdata/<app_id>/。out 至少 64 字节。 */
esp_err_t sdgoods_appdata_path_for(const char *app_id, char *out, size_t out_len);

/* ---- 设备模式检测（见 §3.3） ------------------------------------------- */
/* 设备当前是单应用还是多应用启动模式。 */
sdgoods_device_mode_t sdgoods_device_mode(void);      /* SDGOODS_MODE_SINGLE / _MULTI */
const char *sdgoods_device_mode_str(void);            /* "SINGLE" / "MULTI" */

/* 本固件是否「被启动器管理」（= 从 ota_N 启动且出厂区里是启动器）。
   用于决定「返回启动器」入口是否需要出现。 */
bool sdgoods_device_is_managed_app(void);

/* 本固件是否**启动器宿主**。app 一般用不到；只有启动器自己的代码该关心。 */
bool sdgoods_device_is_launcher_host(void);
```

配套还有应用外壳 `sdgoods_app_shell`（来自平台层 `sdgoods_board`，经 `sdgoods_board.h` 总入口引入）：
`sdgoods_app_shell_bind / set_exit_cb / set_pause_cb / set_resume_cb / leave / menu_open / menu_close / menu_is_open`。所有 app 都要接它，见 §3。

**接上外壳就自动获得三样东西**，都不用写代码：
- **设备级手势策略**（点按位移守卫 / 所有权锁定 / 装饰物穿透）—— §3.1
- **控制中心**（顶部下滑唤出：音量 / 亮度 / 数据 / 电量 / Power|Exit）—— §3.2
- **电源键短按语义**（浮层开着先关浮层；app 内回主页；主页熄屏低功耗）—— §3.2

---

## 2. ★ 返回入口：平台已默认提供，你只需别把它堵掉

**平台默认就给了返回入口**：只要接了应用外壳（§3），多应用模式下**顶部下滑唤出的控制中心里
第 5 个按钮就是 `Exit`**（返回启动器）；单应用模式下同一个位置显示 `Power`（关机）。
判定由 `sdgoods_device_is_managed_app()` 自动完成，app 侧零代码 —— 详见 §3.3。

不过仍**强烈建议** app 在自己的界面里也留一个显式的返回入口：控制中心是「手势唤出」的，
对不熟悉的用户不够显眼。自建入口就调 `sdgoods_return_to_launcher()`：

```c
#include "sdgoods_app_sdk.h"

/* 界面里「返回启动器」按钮的回调 */
static void on_return_to_launcher(lv_event_t *e)
{
    (void)e;
    /* 直接重启进启动器（factory）。清屏资源交给 set_exit_cb 即可。 */
    esp_err_t r = sdgoods_return_to_launcher();
    if (r != ESP_OK) {
        /* 只有「不是被启动器管理的 app」才会走到这里（单应用固件 / 启动器宿主），
         * 此时平台**不会重启**，日志里有 `return-to-launcher refused: ...` 说明原因。
         * ⇒ 按钮最好先判 sdgoods_device_is_managed_app() 再显示（见 §3.3）。 */
    }
}
```

> 为什么必须有这么一条路：app 是完整固件，重启后由 bootloader 直接进 `ota_N`，**不会自动回启动器**。
> 一条能回去的路都没有的话，用户进 app 就「出不来」，只能硬重启——这是上架的否决项。
> 平台默认的 `Exit` 按钮已经兜住了这条底线，但显式入口体验更好。

### 退出时的资源清理（务必接 `set_exit_cb`）

`sdgoods_return_to_launcher()` 会重启，重启前应用外壳会先回调你注册的 `exit_cb` 让你释放资源（定时器、heap 缓冲、把 `s_scr` 置 NULL 防悬垂）。**名字别叫 `on_exit`**（`libc` 有同名函数，会报 conflicting types）。

```c
static void on_menu_exit(void)
{
    /* 若有 lv_timer：lv_timer_del(s_timer); s_timer = NULL; */
    /* 若有大缓冲：heap_caps_free(s_buf); s_buf = NULL; */
    s_scr = NULL;   /* 必须：否则下次进来会切回一个已死的屏 */
}
```

> 单应用模式无此顾虑：用 `sdgoods_app_shell` 顶部下滑菜单的「退出」即可回到应用页，那是 `sdgoods_app_shell_leave(false)`，不重启。

---

## 3. 标准应用外壳接入（每个 app 都要做）

在 `ui_<name>_show()` 里、`lv_scr_load(s_scr)` 之后接这 4 行（顺序别改，`bind` 要在 `lv_scr_load` 之后，因为它要往当前屏挂手势捕获层）：

```c
sdgoods_app_shell_bind(s_scr);                  /* 1. 顶部下滑出菜单（音量/退出） */
sdgoods_app_shell_set_exit_cb(on_menu_exit);    /* 2. 退出/返回时清理（切屏后才回调） */
sdgoods_app_shell_set_pause_cb(on_pause);      /* 3. 菜单打开时暂停（游戏类需要） */
sdgoods_app_shell_set_resume_cb(on_resume);    /* 4. 菜单关闭时恢复 */
```

每帧推进函数 `ui_<name>_poll()` 由主循环约每 2ms 调一次，硬性要求：
- 不在前台立刻 `return`（别白白占主循环）；
- 内部绝不做阻塞（禁 `vTaskDelay` / 等信号量 / 阻塞读）；周期性逻辑用 `lv_timer_create` 或独立 FreeRTOS 任务。

想从模板快速起项目：`python3 tools/new_app_project.py myapp`，它会基于 `app_template.c` 派生一个独立应用工程并自动注册（见 skill `sdgoods-new-app` 与 `tools/new_app_project.py` 顶部说明）。

### 3.1 设备级手势契约（随外壳自动生效，无需额外代码）

整套手势是**设备级统一口径**：启动器与所有 app 共用同一份实现（平台层 `sdgoods_tap.{h,c}`），
不因 app 不同而变。你只要按 §3 接了外壳，就自动获得下表里的全部行为。

| 手势 | 设备级行为 | 谁负责 |
|---|---|---|
| 点按控件 | 触发该控件自己的动作 | 控件自己 —— **必须**用 `sdgoods_tap_bind()` 绑（见下「红线」） |
| 横向拖动 | 滚动可滚动容器（列表 / 图标行） | LVGL 原生，无需处理 |
| 从屏幕**最上边**往下滑 | 打开**控制中心**（音量 / 亮度 / 数据 / 电量 / Power\|Exit） | 应用外壳 `sdgoods_app_shell_bind()`，见 §3.2 |
| 从屏幕**最下边**往上滑 | 回主页（多应用模式 = 回启动器） | **不默认绑定**，需要时 `sdgoods_swipe_up_bind(scr, cb)` |
| 从屏幕**最左边**往右滑 | 返回上一级 | **不默认绑定**，需要时 `sdgoods_swipe_back_bind(scr, cb)` |
| 其余滑动 | 什么都不做（只记日志） | 平台层守卫 |

> 底部上滑 / 左缘右滑**刻意不默认绑**：它们会和 app 自己的底部 UI、横向滑块抢手势，
> 是否接管应由 app 决定。要接就调对应的 `_bind()`，平台层已经保证它们与页面内的
> 按钮 / 滑块互不误触（收紧后的判定：起手点必须在最左 24px 窄带内、水平位移 >64px
> 且明显以横向为主）。

**平台层自动替你做的三件事**（`sdgoods_app_shell_init()` 内部调 `sdgoods_tap_install()`，
对整屏落实策略；app 侧无需写任何代码）：

1. **所有权锁定**（`LV_OBJ_FLAG_PRESS_LOCK`）：手势在哪个对象上按下就归谁，中途掠过
   别的按钮不会被「接管」。⚠️ LVGL **只对「有父对象」的控件**默认加这个标志
   （`lv_obj.c:438`），而 app 的屏是 `lv_obj_create(NULL)` 建的、**没有父对象** ⇒
   平台层替它补上。不补的后果很具体：从空白处上滑、手指滑到某个按钮上再松手，
   该按钮会收到「按下即松开」→ 位移 0 → 被判成**点按**而误触发。
2. **点按位移守卫**：按下期间逐输入周期跟踪最大位移，超过 `SDGOODS_TAP_SLOP`（24px）
   即判为滑动、不触发动作。
3. **装饰物触摸穿透**：`lv_canvas` **默认是可点击的**，压在按钮上会吃掉按钮的点击；
   平台层统一清掉它的 `CLICKABLE`，让触摸穿透到下面真正的交互对象。
   （`lv_label` 构造时本就不可点击，无需处理。）

⚠️ **两条红线**（踩了等于绕过整套守卫）：

- **别用 `lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, ...)` 直接给动作按钮绑回调**，
  改用 `sdgoods_tap_bind()`。否则「按在按钮上滑走再松手」也会触发按钮 ——
  这正是历史误触的成因：
  ```c
  static sdgoods_tap_ctx_t s_ok_ctx;      /* 静态存储，生命周期需覆盖该对象 */
  static void on_ok(void *ud) { (void)ud; /* ... */ }
  sdgoods_tap_bind(btn, &s_ok_ctx, on_ok, NULL);   /* 内部自动加 PRESS_LOCK */
  ```
- **别把一个「要交互的 canvas」直接当按钮用**。要在 canvas 上做交互（画板 / 手写），
  先声明它是「有意交互」的，否则会被当成装饰物穿透：
  ```c
  lv_obj_t *pad = lv_canvas_create(scr);
  sdgoods_tap_keep_interactive(pad);     /* 排除出「装饰物穿透」，并补 PRESS_LOCK */
  ```

**自己动态建屏 / 建控件时**：界面构建完成后补调一次 `sdgoods_tap_normalize(scr)` 即可。
即使漏了也基本安全：平台层有一个 100ms 的看门狗，检测到「当前屏换了」会自动再落实
一次（代价只是一次指针比较），只是最多有 100ms 延迟。

### 3.2 控制中心（随外壳默认自带，无需额外代码）

**每个 app 默认都有控制中心**。顶部下滑唤出的「控制中心」是**设备级统一系统浮层**
（实现：平台层 `components/sdgoods_launcher/src/sdgoods_cc.c`），一级页布局：

```
        Control Center
   Volume   Brightness   Data        ← 上排 3 个
   Battery  Power|Exit                ← 下排 2 个，左对齐
             Mode MULTI                ← 底部小字：当前启动模式
```

| 按钮 | 行为 |
|---|---|
| Volume / Brightness | 进二级滑块页，实时调节（落盘 NVS，键 `sdg_cc`） |
| Data | 进数据页：`Slot 已装/总槽`、`RAM Free 可用/总 KB`、`MEM Free x.x MB`（全部实测） |
| Battery | 进电量页：`Voltage x.xxV` + `Level nn%`（3.00V=0%、4.28V=100% 线性映射） |
| **Power** | **单应用模式**：关机（与启动器一致） |
| **Exit** | **多应用模式**：`sdgoods_return_to_launcher()` 返回启动器 |

二级页操作：左缘右滑回一级；底部横条上滑关掉整个控制中心回 app。

app **不需要**为控制中心写任何代码，也**不要**自己再实现一套类似的浮层。
需要知道「控制中心是否正盖在我上面」时（例如游戏要暂停推进）：

```c
/* 可选：控制中心开 / 关时平台会回调你注册的 pause / resume（复用外壳的语义） */
sdgoods_app_shell_set_pause_cb(on_pause);     /* 浮层打开 -> 暂停 */
sdgoods_app_shell_set_resume_cb(on_resume);   /* 浮层关闭 -> 恢复 */
```

标题文案是英文（`Control Center` / `Volume` / `Brightness` / `Data` / `Battery` / `Power` /
`Exit` / `Mode`），与全设备 UI 文案口径一致，无需 app 处理。

### 3.3 单应用 / 多应用启动模式检测

同一个 app 可能在两种模式下运行，用途不同（最典型：决定「返回启动器」入口是否出现）。
平台层按**运行分区 + 分区内固件身份**自动判定，app 直接查即可：

```c
#include "sdgoods_app_sdk.h"

if (sdgoods_device_is_managed_app()) {
    /* 由启动器装进 ota_N 并拉起 ⇒ 多应用模式 ⇒ 界面上给「返回启动器」入口 */
} else {
    /* 自己就是主机固件（单应用模式，含自更新后运行在 ota_N 的情形）
     * ⇒ 没有「启动器」可回，别给该入口 */
}
```

| 函数 | 语义 |
|---|---|
| `sdgoods_device_mode()` | 设备模式：`SDGOODS_MODE_SINGLE` / `SDGOODS_MODE_MULTI` |
| `sdgoods_device_mode_str()` | 同上，字符串 `"SINGLE"` / `"MULTI"`（可直接显示） |
| `sdgoods_device_is_managed_app()` | **本固件是否由启动器管理**（见下方口径 2） |
| `sdgoods_device_is_launcher_host()` | 本固件是否**启动器宿主**（只有启动器该做的事 gate 在它上面） |
| `sdgoods_device_boot_partition()` | 当前运行分区标签，排障用 |

判定口径（想自己核对时用）：

1. **运行在出厂区** ⇒ 看分区里这份固件的 `project_name`：
   - 是启动器宿主（`SDGOODS_LAUNCHER`）⇒ 设备处于 **MULTI**（它管理着若干 `ota_N`），
     但它自己**不是被管理的 app** ⇒ `is_managed_app() == false`、`is_launcher_host() == true`。
   - 不是启动器 ⇒ 这是「单应用主机固件」⇒ **SINGLE** ⇒ `is_managed_app() == false`。
2. **运行分区不是出厂区**（subtype ≠ `ESP_PARTITION_SUBTYPE_APP_FACTORY`，即从 `ota_N` 启动）
   ⇒ 这里有两种**完全不同**的来路，光看「不是 factory」分不开：
   - 出厂区里装的是**启动器** ⇒ 只有这种设备才存在「被启动器管理的 app」⇒ **MULTI**，
     `is_managed_app() == true`（界面给 `Exit`）。
   - 出厂区里装的**不是**启动器 ⇒ 这台设备跑的是单应用固件，它只是**自己做了标准 A/B
     自更新**才运行在 `ota_N` 上（出厂区里躺着它的旧版本）⇒ 仍是 **SINGLE**，
     `is_managed_app() == false`（界面给 `Power`）。

> ⚠️ **单应用固件做自更新后，模式必须仍是 SINGLE。** 若把口径 2 简化为「不是 factory
> 就是 MULTI」，单应用固件更新完就会被判成 MULTI：控制中心的第 5 个按钮从 `Power` 变
> `Exit`，用户一点就 `esp_ota_set_boot_partition(factory)` + 重启 ⇒ **回滚到更新前的旧
> 版本**（现象：「我刚更新的固件被退回去了」），底部还错显 `Mode MULTI`。
> 所以：**别给单应用固件往 factory 里刷启动器。**

> ⚠️ 别用分区 **label** 去比 `"factory"` —— 本工程出厂区的 label 就叫 `launcher`
> （`partitions.csv`：`launcher, app, factory, 0x10000, 0x300000`），比 label 会永远为假。
> 权威判据只有 subtype。平台层已按 subtype 实现，app 不必自己写。

### 3.4 平台 API 的模式闸门（哪些能调、哪些调了会被拒）

平台层从 2026-09-19 起给「只有某类固件该做的事」加了**权限闸门**（`components/sdgoods_launcher/src/slot_manifest.c`
的 `deny_unless_host()`）。被拒绝时返回 `ESP_ERR_INVALID_STATE(0x103)` 并打一条含模式、
运行分区、固件身份的 ERROR 日志，**不会有任何副作用**（不写 flash、不切启动分区、不重启）：

| API | 谁可以调 | 调错的后果（若没有闸门） |
|---|---|---|
| `sdgoods_return_to_launcher()` / `launch_slot(idx<0)` | **仅被启动器管理的 app** | 单应用固件会「指向自己 + 重启」⇒ 莫名重启，用户以为固件被退回去了 |
| `sdgoods_launcher_launch_slot(idx>=0)` | **仅启动器宿主** | app 能自己切 otadata，把启动器的簿记权交出去 |
| `sdgoods_launcher_install()` / `uninstall()` | **仅启动器宿主** | app 能改写别人的槽与 manifest（otadata 与 manifest 错位） |
| `sdgoods_launcher_self_check()` / `orphan_appdata_cleanup()` / `boot_check()` | **仅启动器宿主** | 非启动器设备上没有「已装槽」⇒ 全部 appdata 目录被判成孤儿 ⇒ **开机把 app 自己的数据删光** |
| `slot_count()` / `list_installed()` / `find_free_slot()` | 任何固件（只读，无闸门） | —— |

对 app 的实际影响只有一条：**别在自建界面里无条件调 `sdgoods_return_to_launcher()`**，
先判 `sdgoods_device_is_managed_app()`（§3.3）；直接调也不会出事，只是「按了没反应」+ 日志可查。

> ⚠️ **app 的 `main.c` 里不要调 `sdgoods_launcher_boot_check()`**（本示例工程 app0 早期版本
> 有这一行，2026-09-19 已移除）。它不是「顺手做点好事」，而是**启动器的职责**：
> 单应用模式下本机没有「已装槽」⇒ 孤儿清理会把**本 app 自己的 `appdata/<app_id>/`** 当成孤儿
> 删掉（每次开机删一次，持久数据全没）。平台层闸门会拦下它并打一条 `self_check refused` ERROR，
> 所以留着也只会刷错误日志 —— 直接删掉那一行即可。

**自测探针**（串口，零代码）：任何 app 里发 `'r'` = 请求返回启动器、`'L'` = 请求启动 slot 0。
在**该被拒绝**的固件里应当看到 `refused: ...` 且**不重启**；在被管理的 app 里 `'r'` 应当真的
重启回启动器（日志出现 `Loaded app from partition at offset 0x10000`）。启动器宿主里还有
`'l'`（正路：启动 slot 0）。这几个字符走的是**串口**、不是合成触摸，所以不受 USB 端口幻触影响。

### 3.5 手势自检：无人手也能验证手势（调试用）

手势行为没法用肉眼远程观察，「点按 vs 滑动」又最容易写错。平台层内置了**合成触摸**能力：
任何 app（**哪怕一行调试代码都没写**）都能在串口上直接验证手势判别。

| 串口按键 | 合成的手势 |
|---|---|
| `1` | 点按屏中央 —— 验证按钮命中 / 点按判定 |
| `2` | 顶部下划 75px —— 打开控制中心（外壳手势） |
| `3` | 底部上划 75px —— 返回主页 / 关闭浮层 |
| `4` | 左缘右滑 75px —— 返回上级 |
| `5` | 从 (70,157) 上划 75px —— 验证「滑动掠过不算点按」 |
| `0` | 点按控制中心第 5 个按钮位 (180,218) —— `Power` 或 `Exit`（验退出语义用） |
| `r` | 请求**返回启动器** —— 验模式闸门：被管理的 app 真重启回启动器；其它固件被拒且不重启（§3.4） |
| `L` | 请求**启动 slot 0** —— 验槽管理闸门：只有启动器宿主成功，app 里被拒（§3.4） |

原理：临时把触摸 indev 的 `read_cb` 换成合成器，产生「按下 → 逐帧移动 → 松开」序列，序列
结束那一帧立刻还原真实驱动。对 LVGL 而言与真手指**完全等价**，所以看串口日志就能断言走了
哪个分支（如 `slot 0: swipe 50 px ... -> not a tap, ignore`）。

要合成任意坐标 / 位移时（例如验证自己的滑块）：

```c
#include "sdgoods_tap.h"

/* 从 (70,180) 每帧右移 25px、共 3 帧 = 累计右划 75px（可从任意任务调用） */
sdgoods_tap_synth(70, 180, 25, 0, 3);
```

⚠️ 仅供调试，不要在正常交互路径里调用。

---

## 4. 持久数据：appdata 优先，**拿不到必须能退 NVS**

所有用户生成内容（设置、缓存的 GIF/字体、进度存档等）优先写**高 16M 的 `appdata` 分区**，按 `app_id` 隔离。**写入低 16M（launcher / `ota_N`）是禁止的**，平台刷机也绝不碰高 16M（设计稿 §1 硬约束）。

> ⚠️ **但 `appdata` 不是「一定存在」的**（2026-09-19 补记，此前文档漏了这条）：
> 平台的多应用固件（启动器 / app0）分区表里有它；**单应用固件若自带精简分区表
> （只留 `nvs` + `factory`），就没有**。此时 `sdgoods_appdata_*` 返回
> `ESP_ERR_NOT_FOUND`，`/appdata/...` 这个路径压根不存在 ——
> **拿着拼出来的路径直接 `fopen` 会失败，或写坏别处**。
> 所以每个 app 都要能「没有 appdata 也照常跑」。官方推荐的写法只有一条：

```c
#include "sdgoods_app_sdk.h"
#include "sdgoods_nvs.h"      /* sdgoods_nvs_ensure：平台层统一的 NVS 保障 */
#include "nvs.h"
#include "esp_app_desc.h"     /* ⚠️ 是 esp_app_desc.h —— 不是 esp_app_format.h！
                                 esp_app_desc_t 与 esp_app_get_description() 都在前者里，
                                 写错名字会得到 `unknown type name 'esp_app_desc_t'` */

#define NVS_NS "myapp"

/* 本 app 的 app_id = IDF project name（CMakeLists.txt 里 project(<name>) 的那个名字）。
 * 卸载 / 开机孤儿清理会据此整目录回收 /appdata/<app_id>/。 */
static const char *my_app_id(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->project_name : "unknown_app";
}

/* ★ 一条调用拿到「已建好」的私有目录；拿到就写文件，拿不到就退 NVS。 */
void save_progress(int level)
{
    char dir[64];
    if (sdgoods_appdata_begin(my_app_id(), dir, sizeof(dir)) == ESP_OK) {
        char path[96];
        snprintf(path, sizeof(path), "%s/save.dat", dir);
        FILE *f = fopen(path, "wb");          /* 大对象 / 多文件走这里 */
        if (f) { fwrite(&level, sizeof(level), 1, f); fclose(f); }
        return;
    }
    /* 没有 appdata（单应用精简分区表）或挂载失败 —— **这是正常返回值，不是异常** */
    sdgoods_nvs_ensure();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "level", level);
        nvs_commit(h);
        nvs_close(h);
    }
}
```

**两条路怎么选**：

| 数据 | 走哪条 | 理由 |
|---|---|---|
| 存档、缓存、GIF/音频、日志、多文件 | `appdata` 文件 | 16MB 容量、写入便宜、不消耗 NVS 擦写寿命 |
| 设置、开关、进度计数（几百字节以内） | NVS 键值 | 一定存在 —— `nvs` 是所有分区表的最小公约数 |
| 必须长期留存的重要数据 | 平台侧 | 卸载 app / 孤儿清理会整目录删掉 appdata |

**参考实现**：`main/app_data_store.c`（app0 自带）—— 开机探测后端 + 写一次计数，
日志会打出 `appdata partition present=yes/no` 与 `backend=appdata|NVS`，可直接照抄；
DEMO →「其他」页的 `数据: ...` 一行显示的就是它实测出来的**真实后端**。

要点：
- ⚠️ **工程里必须开 FAT 长文件名**（`CONFIG_FATFS_LFN_HEAP=y` + `CONFIG_FATFS_MAX_LFN=64`，
  见 `BUILD.md` §2.1）：`/appdata/<app_id>/` 的目录名就是 `project_name`，**必然超过
  FAT 的 8.3 短名限制**。不开的话 `mkdir` 拿到 `EINVAL(22)`，目录建不出来、
  数据静默落空 —— 而 ≤8 字符的短名字却看着正常，很容易误判「没问题」。
- **想知道「现在到底存在哪」**：`sdgoods_appdata_available()`（只查分区表、不挂载）或
  `sdgoods_appdata_begin()` 的返回值。界面上要显示就显示真值，别写死 "appdata"。
- **目录隔离**：`sdgoods_appdata_begin()` 返回 `/appdata/<app_id>/`，不同 app 互不越界；
  它会把目录 `mkdir` 好，**不用自己建**。只想拿路径不建目录才用 `_path_for()`。
- ⚠️ **别自己写 `nvs_flash_init()`**：最小 app 不碰 WiFi/BLE 时从不初始化 NVS，
  直接用 `sdgoods_nvs_ensure()`（幂等，内部含擦除重试）。
- **挂载幂等 / 卸载要小心**：`sdgoods_appdata_mount()` 重复调用安全；但控制中心的数据页会
  「挂载 → 读 → 卸载」，所以**页面打开之后再读文件前要再 `mount()` 一次**（幂等，代价极低）。
- **卸载即清空**：用户卸载 app，或开机孤儿清理发现该 `app_id` 已无已装槽，整目录 `rm -rf`。
  隐私默认不保留、不询问 —— **别把必须长期留存的东西只放 appdata**。
- **`app_id` 必须稳定**：用 `project_name`（与你在平台上的工程一致）当目录名。
  改名会让旧数据变成孤儿、下次开机被清掉。

---

## 5. 最小可上架示例

一个「点按钮计数 + 返回启动器」的骨架（`ui_myapp.c` 节选）：

```c
#include "myapp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "sdgoods_board.h"        /* 平台层总入口：屏/触摸/音频/框架/栅格 */
#include "sdgoods_app_sdk.h"      /* 应用 SDK：返回启动器 + appdata */

static const char *TAG = "myapp";
static lv_obj_t *s_scr, *s_lbl;
static int s_level;

static void on_menu_exit(void) { s_scr = NULL; s_lbl = NULL; }

static void on_return(lv_event_t *e) {
    (void)e;
    sdgoods_return_to_launcher();          /* ★ 返回启动器（硬要求） */
}

static void on_tap(lv_event_t *e) {
    (void)e;
    s_level++;
    if (s_lbl) { char b[32]; snprintf(b, sizeof(b), "Lv %d", s_level); lv_label_set_text(s_lbl, b); }
}

void ui_myapp_show(void)
{
    if (s_scr) { lv_scr_load(s_scr); return; }

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);

    s_lbl = lv_label_create(s_scr);
    lv_label_set_text(s_lbl, "Lv 0");
    lv_obj_align(s_lbl, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *tap = lv_btn_create(s_scr);
    lv_obj_align(tap, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(tap, on_tap, LV_EVENT_CLICKED, NULL);

    /* 返回启动器按钮 */
    lv_obj_t *ret = lv_btn_create(s_scr);
    lv_obj_align(ret, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(ret, on_return, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rt = lv_label_create(ret);
    lv_label_set_text(rt, "返回启动器");
    lv_obj_center(rt);

    lv_scr_load(s_scr);

    sdgoods_app_shell_bind(s_scr);
    sdgoods_app_shell_set_exit_cb(on_menu_exit);
}

void ui_myapp_poll(void)
{
    if (!s_scr) return;   /* 不在前台 */
}
```

---

## 6. 上架前端侧检查清单

1. **返回入口**：控制中心的 `Exit` 按钮已由平台默认提供（§3.2）；界面里**另有**显式「返回启动器」
   入口更好（调 `sdgoods_return_to_launcher()`），且**别在单应用模式下显示它**（用
   `sdgoods_device_is_managed_app()` 判，见 §3.3）。
2. **别自己实现系统浮层**：控制中心已默认自带（§3.2），不要重复做音量/亮度/退出的浮层。
3. **数据不越界**：持久数据只走 `sdgoods_appdata_*`，`app_id` 用稳定 `project_name`。
4. **新中文文案**：只要加了新中文，重跑 `python3 tools/gen_fonts.py`（首次先 `tools/fetch_fonts.py`），否则屏上方块（tofu）。英文文案不动字体。
5. **体积**：`pack_app.py` 上限 3MB，安全线 **≤2.9MB**，超限拒收。
6. **接应用外壳**：`bind` + `set_exit_cb`（+pause/resume）四行齐全；`poll` 不阻塞、不在前台就 return。
7. **手势没被绕过**（§3.1 两条红线）：动作按钮都走 `sdgoods_tap_bind()`，没有直接用
   `LV_EVENT_CLICKED` 绑动作；要交互的 canvas 已调 `sdgoods_tap_keep_interactive()`。
8. **自证**：
   - 截屏：串口发 `s`（设备回 `===SHOT-BEGIN ... ===` + JPEG 原始字节），或平台侧工具批量抓图。
   - 手势：串口发 `2` 应能唤出控制中心、发 `1` 应是点按、发 `5` 应被判成滑动 —— §3.5 自检按键表。
   - 模式：控制中心底部小字应显示符合预期的 `Mode SINGLE` / `Mode MULTI`。

清单 1–7 是端侧 SDK 要求；**出包与提交到平台**走 `docs/PUBLISHING.md`（单文件 `build/<项目>.bin` → `tools/pack_app.py` 校验 → `tools/sdgoods_publish.py` 发布）。

---

## 7. 与平台安装流程的关系（背景，无需 app 处理）

多应用模式安装是**设备拉模型**（设计稿 §5）：启动器主动 `POST /api/devices/me/install` 拿到 `{slotIndex, downloadUrl, sha256}`，下载 app.bin 烧进 `ota_{slotIndex}`，校验 `esp_app_desc_t` + sha256，再 `POST /slots/sync` 回报平台。app 本身不参与这套协议——它只要按本文档把「返回入口 + appdata」接好即可。单应用模式则完全不走这套，等价于直接刷 app 固件。
