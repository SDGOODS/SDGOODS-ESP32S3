# AGENTS.md —— 给 AI 编程助手的开发约定

> 本文件是写给 **AI 编程助手**（Claude Code / Cursor / Copilot / Codex 等）看的项目说明书。
> 你在为本仓库改代码前，请先读完本文件；人也可以读，但重点在"约束"。
>
> 目标读者是**谷仓 SDGOODS 开放平台的用户**：他们用 AI 辅助，在这块圆屏设备上做二次开发。

---

## 0. 一句话说清项目

ESP32-S3 + 360×360 圆屏的桌面设备固件。ESP-IDF 5.5 + LVGL 8.3.11。
纯 C，没有第三方 UI 框架。

工程**刻意分成两层**，这是本仓库最重要的一条结构性约定：

```
components/sdgoods_board/     平台层（板级支持包）
    include/  对外接口（应用层唯一该 include 的地方：sdgoods_board.h）
    src/      实现：LVGL 移植、触摸、音频、应用框架、开机流程
    lcd/      ST77916 QSPI 面板驱动
    fonts/    中文子集字体（由 tools/gen_fonts.py 生成）

main/                         应用层
    main.c        装配点：初始化平台 + apps_register() 接线
    apps/         ★ 用户的应用写在这里
    patches/      对 LVGL 的补丁（自动应用）
```

**改代码前先问自己：这是应用层的事，还是平台层的事？**
99% 的二次开发需求（加应用、改玩法、改界面）都只在 `main/apps/` 里做。

---

## 1. 编译与烧录（照抄，不要自创命令）

```bash
# 编译（首次会自动下载 LVGL，并自动应用 main/patches 里的补丁）
idf.py set-target esp32s3     # 仅首次
idf.py build

# 烧录 + 看日志
idf.py -p <串口> flash monitor
```

⚠️ **有几个坑，务必按下面的方式做：**

1. **每次改完必须真编译**（`idf.py build`），不要凭"看起来对"就说改完了。
   `-Werror=all` 是开着的，警告会直接变成错误。
2. **如果用户的工作副本是「编辑目录 / 构建目录分离」的**，改了源码要先同步到构建目录再编译
   —— 具体路径问用户，不要猜。同步时**整个 `components/` 也要一起同步**（不只是 `main/`），
   而且同步完 `find . -name "*.c" -exec touch {} \;` 强制重编，否则可能烧出新旧混合体。
3. **不要 `rm -rf build`**。要么让 ninja 增量编译，要么换一个新的 `-B build_xxx` 目录。
4. 串口号会变（S3 原生 USB-Serial-JTAG 的编号与芯片 MAC 绑定），**不要把串口写死**，
   先 `ls /dev/cu.usbmodem*` 或 `ls /dev/ttyACM*` 看当前是什么。
   烧录前若报 `Resource busy`，用 `lsof /dev/cu.usbmodemXXXX` 查是谁占着
   （常见：浏览器打开了 Web Serial 页面，关掉即可）。
5. **不要改 `managed_components/`**。它是组件管理器下载的，会被重新覆盖；
   要改 LVGL 就改 `main/patches/` 下的补丁文件（见第 6 节）。

---

## 2. 两层边界：什么能改，什么不能改

### 硬约束

- **平台层不能 `#include` 应用层的头文件**（如 `ui_home.h`、`ui_plane.h`）。
  平台层需要"回主页""有新应用要轮询"这类信息时，走注册接口 —— 见下一小节。
  反过来（应用 `#include "sdgoods_board.h"`）是正常且推荐的。
- **新增应用必须注册**。只写了 `ui_my_app.c` 而不改 `apps_registry.c`，表现是
  「编译通过、但启动台没有按钮、poll 也不被调用」。用 `tools/new_app.py` 可自动完成。
- **不要动 `components/sdgoods_board/` 里的这些**（调过参数、有实测依据）：
  - `src/lvgl_port.c` 的绘制缓冲配置（内部 SRAM 双缓冲 8 行 + 异步 flush；
    改回 PSRAM 或调大 queue 会出黑条/红线）
  - `lcd/st77916.c` 的 vendor 初始化序列与 SPI 队列深度/分片大小
  - `src/audio_recplay.c` 的 I2S 与功放启停时序（关功放有 64ms 淡出防爆音）
- **不要改许可分层**（这是对外的法律承诺，不是注释）：

  | 目录 | 许可 | 能不能改 |
  |---|---|---|
  | `components/sdgoods_board/` | Apache-2.0 | ✅ 可商用，保留声明即可 |
  | `main/`（应用层） | PolyForm NC 1.0.0 | ⚠️ 非商业，商用要授权 |
  | `main/patches/` | MIT | ❌ **不可改** —— LVGL 衍生，我们无权追加限制 |
  | `components/sdgoods_board/fonts/` | SIL OFL 1.1 | ❌ **不可改** —— Noto Sans SC 衍生 |

- **新建 `.c` / `.h` / `.py` 必须带许可头**。写完之后跑一次：

  ```bash
  python3 tools/add_license_headers.py --apply   # 幂等，已带声明的会跳过
  ```

  漏了不会编译报错，但一个新文件没有任何声明 = 许可不明，
  会让认真读许可的人不敢用（这跟"能不能编译"是两件事）。
  `main/patches/` 与 `fonts/` 会被脚本刻意跳过，那是正确的。

### 平台层需要回调应用层时：注册制

平台层不知道主页长什么样、有哪些应用。它只认 `sdgoods_hooks.h` 里这几个函数指针，
由 `main/apps/apps_registry.c` 填上，`main.c` 调 `apps_register()` 生效：

```c
sdgoods_apps_set_poll(fn);          /* 平台主循环每轮调用 → 汇总各应用的 *_poll() */
sdgoods_ui_set_nav(&nav);           /* nav = { home_create_show, home_show, apps_show } */
```

`lvgl_port_loop()` 里的 `sdgoods_apps_poll()` 就是这条链路。
**你不需要改平台层就为了让新应用跑起来** —— 往 `apps_registry.c` 的 `s_apps[]` 表里加一行即可。

---

## 3. 加一个新应用（标准姿势）

```bash
python3 tools/new_app.py my_app "我的应用"
```

脚本做三件事（也是手改时要做的三步）：

1. 复制 `main/apps/app_template.c/.h` → `main/apps/ui_my_app.c/.h`，替换应用名与按钮文字；
2. 往 `main/CMakeLists.txt` 的 `SRCS` 里插入 `"apps/ui_my_app.c"`
   （插在 `# >>> new_app.py: ... >>>` 标记之前）；
3. 往 `main/apps/apps_registry.c` 插入 `#include` 与 `s_apps[]` 表项
   （同样有 `>>>` 标记定位）。

**不要删那两个 `>>>` 标记** —— 脚本靠它们定位。

新应用要接的框架（`app_template.c` 里已写好）：

```c
ui_app_shell_bind(s_scr);                 /* 1. 顶部下滑出菜单（音量/截屏/退出） */
ui_app_shell_set_exit_cb(on_menu_exit);   /* 2. 退出清理：回调名别叫 on_exit！ */
ui_app_shell_set_pause_cb(on_pause);      /* 3. 菜单打开/关闭时会调（可省） */
ui_app_shell_set_resume_cb(on_resume);
lv_scr_load(s_scr);                       /* bind 要在 load 之后 */
```

**⚠️ `on_exit` 是 libc 里的函数**，用这个名字会直接编译失败
（`conflicting types for 'on_exit'`）。用 `on_menu_exit` 之类的名字。

**LVGL 是单线程的**：所有 LVGL API 只能在 LVGL 主循环
（`components/sdgoods_board/src/lvgl_port.c` 的 `lvgl_port_loop()`）里调用。
其它任务（BLE 回调、音频、串口）只能改标志位，由 `lv_timer` 或页面自己的 `*_poll()` 消费。

**`*_poll()` 的两条纪律**：

- 不在前台就立刻 `return`（否则白白占用每 2ms 一轮的主循环）；
- 内部**绝不做阻塞操作**（`vTaskDelay` / 等信号量 / 阻塞读串口）。
  需要等待的逻辑放到独立 FreeRTOS 任务里，poll 里只读标志位。

参考实现：`main/apps/app_template.c`（最小完整例子，编译后就是启动台上的「示例」应用）、
`ui_snake.c`（简单）、`ui_plane.c`（最复杂，带游戏循环和联机）。

---

## 4. 改了中文文案 → 必须重新生成字体（最容易忘的一步）

中文用的是**子集字体**（只包含源码里扫描到的字），所以新增任何汉字后不重新生成，
屏幕上就是**方框（tofu）**：

```bash
python3 tools/fetch_fonts.py     # 首次需要：下载 OFL 源字体到 tools/fonts/（不入库）
npm i lv_font_conv               # 首次需要：装字体转换工具
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv
python3 tools/gen_fonts.py --check        # 只校验当前字体是否缺字（很快）
```

脚本会扫描 `main/` 与 `components/sdgoods_board/` 下的全部 `.c/.h`，重新生成
`components/sdgoods_board/fonts/` 下的 4 个字体文件，并逐字校验（缺字直接报错退出）：

| 文件 | 内容 | 用途 |
|---|---|---|
| `cn_font_14/16.c` | 扫到的**全部**字符 | 兜底（fallback），必须一个不缺 |
| `si_yuan_black_icon_14/16.c` | UI 精选子集（`tools/gen_fonts.py` 里的 `ICON_SYMBOLS`） | 主字体，缺字自动回退到 `cn_font_*` |

> 生成的字体文件头部带 **SIL OFL 1.1** 的版权声明（字形来自 Noto Sans SC），
> 这是许可要求，**不要删**。换字体用 `--font <路径>`，并确认对方的再分发授权。
>
> ⚠️ 注释里的中文**也会**被计入字符集（全量扫描，宁可多不可漏）。
> 所以写了很多中文注释后重新生成字体，体积会略增 —— 这是有意的取舍：
> 漏一个字就是屏幕上一个方框，几 KB 体积不算什么。

### 换字体 / 加长文案后：量一下宽度

改了字体文件或把某条文案改长了，**先离线量宽度再烧板子**（烧一次一分钟，量一次一秒）：

```bash
python3 tools/font_metrics.py                       # 内置的本项目关键文案表
python3 tools/font_metrics.py --strings "俄罗斯方块,按电源键返回"
```

它从生成的字体里解析 `glyph_dsc`（前进宽度，1/16 px）与 `cmaps`，算出渲染宽度，
标出**缺字**与**超宽**（默认上限：圆按钮 76px、整行 340px），并跟仓库首个提交的旧字体对比。
注意设备端串口只有 `'s'` 截屏命令、**没有导航命令**，所以"手势到不了的界面"（启动台、菜单）
在烧板子之前只能靠这个工具确认。

---

## 5. 内存：这块芯片最容易踩的坑

- **内部 SRAM 很小**（约 400KB 可用），**PSRAM 有 8MB**。
- 大缓冲（GIF canvas、大图片、大数组）必须走 PSRAM：
  `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
- **但 LVGL 的绘制缓冲不能放 PSRAM**（QSPI DMA 弹跳缓冲预算极小 → 黑条 / 红线）。
  这块已经调好，别动 `lvgl_port.c` 里的缓冲配置。
- AI 常见错误：一次性创建几百个 LVGL 对象 / 大数组开在栈上 →
  运行时报 `NO_MEM` 或直接重启。加功能时优先"复用对象 + 固定大小数组"。
- 圆屏布局：可用区域比矩形屏小，四角会被圆边切掉。用 `sdgoods_ui.h` 的 `SDG_UI_*`
  栅格常量（已避开），别自己算坐标。

---

## 6. 不要直接改 `managed_components/`

LVGL 的 GIF 解码器被本项目改过（canvas 走 PSRAM、RGB565、源数据拷进 RAM），
改动**横跨 3 个文件**（`gifdec.c` / `gifdec.h` / `lv_gif.c`），必须成套替换。

- 补丁文件在 `main/patches/lvgl-8.3.11-gif-psram/`，随源码提交；
- `main/CMakeLists.txt` 在 CMake configure 阶段自动调用
  `main/patches/apply_lvgl_patches.py` 覆盖过去，并**逐字节校验**；
- 只改一半的症状：`too few arguments to function 'gd_open_gif_data'` 编译失败，
  或开机动画颜色发花。

要升级 LVGL 版本，见 `main/patches/README.md` 的升级步骤。

---

## 7. 改飞机游戏的联机逻辑前，先读这四条铁律

（详见 `docs/ARCHITECTURE.md`，**违反任意一条都会让两台设备的敌机分叉**）

1. 世界生成必须走共享 PRNG，且**消费次数固定**（槽满也要消费）；
2. 本地私有行为不能喂共享 PRNG；改变子弹模式必须扩协议字段让对端同公式复制；
3. 会改变敌机集的瞬时事件（如炸弹清屏）必须广播事件序号；
4. **玩家受击不能 despawn 敌机/敌弹** —— 改扣命 + 无敌闪烁。

另外，「同一 tick 的语句顺序」也有硬约束：

```
道具拾取 → 火力倒计时/到期 → 自动开火 → …… → 广播状态包
```

凡是本帧会改变「对端要复制的参数」（`power` 等）的语句，都要排在开火之前；广播在开火之后。

---

## 8. 改完 UI 怎么自己验证（强烈建议）

设备支持**串口一键截屏**，这是 AI 自证 UI 改动的最有效手段 —— 别只靠读代码：

```bash
python3 tools/screenshot_recv.py -p <串口> -o /tmp/shot.png -n 1 -t
# -t: 每 5 秒重发一次触发，收到就停
```

设备侧触发方式：应用内**顶部下滑 → 点「截屏」**（或串口直接发 `s`）。

然后**打开 PNG 看一眼**（AI 可以直接看图）：中文有没有方框、控件有没有重叠、
颜色对不对、有没有被圆边切掉。这一步比读十遍代码有用。

游戏类改动可以用 `tools/plane_stat.py` 边玩边统计道具掉落/档位/受击次数。

---

## 9. 提交前检查清单

- [ ] `idf.py build` 真的过了（不是"应该能过"）
- [ ] 新应用已注册进 `apps_registry.c`（启动台有按钮 + poll 被调用）
- [ ] 新增中文字符已重跑 `tools/gen_fonts.py`，且 `--check` 零缺字
      （文案改长的另跑 `tools/font_metrics.py` 看是否超宽）
- [ ] 没有在平台层 `#include` 应用层的头文件（分层没被破坏）
- [ ] 没有把大数组/大缓冲放到栈上或内部 SRAM
- [ ] 回调名没有撞 libc（`on_exit` / `on_read` / `on_write` 等）
- [ ] 改玩法的话：联机四条铁律 + same-tick 顺序都遵守了
- [ ] `board_pins.h`、`lvgl_port.c` 的缓冲配置没被顺手改掉
- [ ] 新建的 `.c` / `.h` / `.py` 都带了许可头（`python3 tools/add_license_headers.py`）
- [ ] 没动 `main/patches/` 与 `fonts/` 的许可声明（这两处的许可由上游决定，不可更改）
- [ ] 截屏验证过画面（UI 类改动）
