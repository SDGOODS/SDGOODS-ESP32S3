# AGENTS.md —— 给 AI 编程助手的开发约定

> 本文件是写给 **AI 编程助手**（Claude Code / Cursor / Copilot / Codex 等）看的项目说明书。
> 你在为本仓库改代码前，请先读完本文件；人也可以读，但重点在"约束"。
>
> 目标读者是**谷仓 SDGOODS 开放平台的用户**：他们用 AI 辅助，在这块圆屏设备上做二次开发。
>
> **归属**：本工程是**谷仓共创计划**的基础工程，对应**谷仓 SDGOODS 开放平台**与
> **谷仓次元屏（谷仓电子徽章）**设备；上述名称与本基础代码的著作权及相关权利，
> 均归**深圳希德创新网络有限公司（SDGOODS）**所有。
> 改代码时**不要删掉文件头的版权 / Required Notice 声明** —— 那是许可生效条件（见第 9 节清单）。
>
> **联系**：官网 [https://sdgoods.ai](https://sdgoods.ai) · 邮箱 `zhangzuoliang321@126.com`（商业授权 / 报 bug / 合作统一走这个邮箱）。
> 固件里带着它们（开机串口横幅打印官网与邮箱；**关于页显示官网、不展示邮箱**，按产品口径），
> 宏是 `SDGOODS_HOMEPAGE` / `SDGOODS_CONTACT_EMAIL`，定义在 `main/gen_build_version.cmake`
> —— 不要在别处另写字面量。

---

## 0. 改代码前第一步：检查开发环境（必做）

**你（AI）在动任何代码之前，必须先确认用户的环境能编译这块固件。** 直接跑本仓库自带的检查脚本：

```bash
python3 tools/check_env.py            # 人类可读表格；退出码 0=可编译，1=有必需项缺失
python3 tools/check_env.py --json     # 结构化输出，方便你解析
```

逐条处理输出：
- `python3` / `ESP-IDF` / `esptool` 任何一个 `XX`（必失败）且退出码为 `1` → **停下，先帮用户把环境补齐**，
  不要凭「应该能编译」继续。ESP-IDF 报「已安装但未激活」时，先 `source <IDF>/export.sh` 再编译。
- `node/npm` 报 `!!`（缺失）时：如果用户**要改/增中文文案**，先 `npm i lv_font_conv`，
  否则字体生成会失败、烧出满屏方框；若本次不碰中文文案可忽略。
- 完整工具清单、各系统安装步骤、串口驱动见 [`docs/ENVIRONMENT.md`](../docs/ENVIRONMENT.md)。

> 这一步是「开发的基础」：环境不对，后面所有编译 / 烧录 / 截屏自测都没法进行。

---

## 0.5 一句话说清项目

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
  - `src/sdgoods_lvgl.c` 的绘制缓冲配置（内部 SRAM 双缓冲 8 行 + 异步 flush；
    改回 PSRAM 或调大 queue 会出黑条/红线）
  - `lcd/sdgoods_lcd.c` 的 vendor 初始化序列与 SPI 队列深度/分片大小
  - `src/sdgoods_audio.c` 的 I2S 与功放启停时序（关功放有 64ms 淡出防爆音）
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

`sdgoods_lvgl_loop()` 里的 `sdgoods_apps_poll()` 就是这条链路。
**你不需要改平台层就为了让新应用跑起来** —— 往 `apps_registry.c` 的 `s_apps[]` 表里加一行即可。

### 新增 BSP 基础能力（能力登记表 + SDGOODS-CAPS 串口查询）

截屏（`'s'`）是第一个被做成「可选 BSP 基础能力」的功能，模式值得照搬：

- `components/sdgoods_board/include/sdgoods_caps.h` + `src/sdgoods_caps.c`：位掩码能力表
  `SDGOODS_CAP_*`，首位是 `SDGOODS_CAP_SCREENSHOT`；提供 `sdgoods_caps_add/has/names`。
  某能力初始化时调用 `sdgoods_caps_add(...)` 把自己登记进去。
- `components/sdgoods_board/src/sdgoods_console.c`：**常驻串口控制台**（始终编译，不依赖某能力是否编入），
  轮询 USB-Serial-JTAG：收到 `?` 回一行 `SDGOODS-CAPS:SHOT,...`（多能力逗号分隔，未启用任何可选能力则为空串）；
  收到 `s`/`S` 仅当 `sdgoods_caps_has(SDGOODS_CAP_SCREENSHOT)` 才触发截屏。
- 网页端「从设备截图」（`cyb2-dev-site/upload-firmware.html`）先发 `?` 解析能力行：
  有 `SHOT` 才截图；**无 `SHOT`（含两次重试仍无响应）就弹窗提示用户去 BSP 启用该能力并重新烧录**，
  绝不盲发命令（旧/不兼容固件盲截会回颜色错乱的图）。

**新增任何 BSP 基础能力（BLE 配对、Wi-Fi 配网…）都走这套**：登记 `sdgoods_caps` + 让控制台应答 `?`。
这样网页/AI 端能自动探测能力、缺失时给明确指引，而不是静默失败。PC 端可用
`tools/screenshot_recv.py --caps -p /dev/cu.usbmodemXXXX` 验证设备能力。

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
sdgoods_app_shell_bind(s_scr);                 /* 1. 顶部下滑出菜单（音量/退出） */
sdgoods_app_shell_set_exit_cb(on_menu_exit);   /* 2. 退出清理：回调名别叫 on_exit！ */
sdgoods_app_shell_set_pause_cb(on_pause);      /* 3. 菜单打开/关闭时会调（可省） */
sdgoods_app_shell_set_resume_cb(on_resume);
lv_scr_load(s_scr);                       /* bind 要在 load 之后 */
```

**⚠️ `on_exit` 是 libc 里的函数**，用这个名字会直接编译失败
（`conflicting types for 'on_exit'`）。用 `on_menu_exit` 之类的名字。

**LVGL 是单线程的**：所有 LVGL API 只能在 LVGL 主循环
（`components/sdgoods_board/src/sdgoods_lvgl.c` 的 `sdgoods_lvgl_loop()`）里调用。
其它任务（BLE 回调、音频、串口）只能改标志位，由 `lv_timer` 或页面自己的 `*_poll()` 消费。

**`*_poll()` 的两条纪律**：

- 不在前台就立刻 `return`（否则白白占用每 2ms 一轮的主循环）；
- 内部**绝不做阻塞操作**（`vTaskDelay` / 等信号量 / 阻塞读串口）。
  需要等待的逻辑放到独立 FreeRTOS 任务里，poll 里只读标志位。

参考实现：`main/apps/app_template.c`（最小完整例子，无界面入口但一直参与编译，
所以模板不会失效）、`ui_flappy.c`（简单）、`ui_plane.c`（最复杂，带游戏循环和联机）。

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
python3 tools/font_metrics.py --strings "谷仓共创计划,按电源键返回"
```

它从生成的字体里解析 `glyph_dsc`（前进宽度，1/16 px）与 `cmaps`，算出渲染宽度，
标出**缺字**与**超宽**，并跟仓库首个提交的旧字体对比。

⚠️ 两个必须知道的点：

1. **圆屏的可用宽度不是常数。** 屏幕是圆的，同一行文字越靠上/下越窄：
   `可用宽度 ≈ 2*sqrt(180² - dy²)`（dy = 该行离屏幕中心的最远距离）。
   屏幕中心能放满 360px，而关于页 y=232 那行只剩约 **322px**（y 越大越窄）。
   所以文案表给每行都带 **y 坐标**，工具按弦宽自动收紧上限 ——
   只按"整行 340px"判，会在这种位置给出假 OK，而屏幕上是**两端被圆边切掉**。
   圆按钮上的文字不受弦宽限制（按钮自己是圆，按直径 76px 判）。
2. **太长就折行，别硬塞一行。** 限宽 + 居中 + `LV_LABEL_LONG_WRAP`：

   ```c
   lv_obj_set_width(lbl, 200);
   lv_obj_set_height(lbl, LV_SIZE_CONTENT);
   lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
   lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
   ```

   文案表里这种条目要在 `wrap` 字段写折行宽度，工具会**模拟折行后逐行判宽**
   （关于页的英文许可标签就是被这条抓出来的：单行 327px，折成两行后最宽 171px）。

注意设备端串口只有 `'s'` 截屏命令、**没有导航命令**，而且单张 360×360 截屏走
USB-Serial-JTAG 要约 **33 秒**，所以"手势到不了的界面"（启动台、菜单、关于）
在烧板子之前只能靠这个工具确认。
验证 UI 时如需自动走遍界面，可临时加一个"串口数字键 -> 切页"的探针
（见 `docs/ARCHITECTURE.md` §9 末尾的做法），**验证完必须删掉**。

---

## 5. 内存：这块芯片最容易踩的坑

- **内部 SRAM 很小**（约 400KB 可用），**PSRAM 有 8MB**。
- 大缓冲（GIF canvas、大图片、大数组）必须走 PSRAM：
  `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
- **但 LVGL 的绘制缓冲不能放 PSRAM**（QSPI DMA 弹跳缓冲预算极小 → 黑条 / 红线）。
  这块已经调好，别动 `sdgoods_lvgl.c` 里的缓冲配置。
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

设备侧触发方式：**向串口发 `s`**（菜单里已没有截屏按钮；脚本加 `-t` 会自动发）。

然后**打开 PNG 看一眼**（AI 可以直接看图）：中文有没有方框、控件有没有重叠、
颜色对不对、有没有被圆边切掉。这一步比读十遍代码有用。

游戏类改动可以用 `tools/plane_stat.py` 边玩边统计道具掉落/档位/受击次数。

---

## 9. 提交前检查清单

- [ ] `idf.py build` 真的过了（不是"应该能过"）
- [ ] 新应用已注册进 `apps_registry.c`（启动台有按钮 + poll 被调用）
- [ ] 新增中文字符已重跑 `tools/gen_fonts.py`，且 `--check` 零缺字
- [ ] 文案改长/字号改大后跑过 `tools/font_metrics.py`，**圆屏上的整行文本没超弦宽**
      （长文案要像关于页那样限宽折行，别硬塞一行；该行 y 坐标变了要同步改文案表）
- [ ] 没有在平台层 `#include` 应用层的头文件（分层没被破坏）
- [ ] 没有把大数组/大缓冲放到栈上或内部 SRAM
- [ ] 回调名没有撞 libc（`on_exit` / `on_read` / `on_write` 等）
- [ ] 改玩法的话：联机四条铁律 + same-tick 顺序都遵守了
- [ ] `board_pins.h`、`sdgoods_lvgl.c` 的缓冲配置没被顺手改掉
- [ ] 新建的 `.c` / `.h` / `.py` 都带了许可头（`python3 tools/add_license_headers.py`）
- [ ] 没有删掉已有文件头的版权 / `Required Notice` 声明（删掉即失去使用授权）
- [ ] 没动 `main/patches/` 与 `fonts/` 的许可声明（这两处的许可由上游决定，不可更改）
- [ ] 截屏验证过画面（UI 类改动）；临时探针 / 调试代码**已全部删干净**
- [ ] 新增界面文案都用了 `SDG_T("中文", "English")`（不要只写中文）
- [ ] 按钮回调用的是**语言无关的键**，不是按钮文字（改了文字回调会失效）
- [ ] 品牌名、公司名、联系邮箱都从 `build_version.h` 的宏取，没有另写一份字面量
- [ ] 文档里的文件树 / 文件名与真实目录一致（平台层文件都带 `sdgoods_` 前缀）

---

## 10. 把固件提交到谷仓 SDGOODS 开放平台（AI 也能做）

开发完、本地截屏自测通过后，把固件交到 **谷仓 SDGOODS 开放平台**（广场）。完整说明在
[`docs/PUBLISHING.md`](docs/PUBLISHING.md)，这里给 AI 最短路径：

1. **一次性登录**（邮箱收 4 位码，refreshToken 缓存在 `~/.sdgoods/credentials.json`）：

   ```bash
   export SDGOODS_API_BASE=https://sdgoods.ai/api
   python3 tools/sdgoods_publish.py login 你的邮箱@example.com
   ```

2. **提交**（自动用缓存的 refreshToken 换新 accessToken，无需再验证码）：

   ```bash
   python3 tools/sdgoods_publish.py publish \
     --file build/SDGOODS_EBADGE.bin --name "我的固件" \
     --desc-zh "中文简介" --desc-en "English intro" \
     --category game --shots shot1.png shot2.png
   ```

不想要这个工具、直接调接口也行：`tools/sdgoods_publish.py` 就是「鉴权 →
`POST /api/uploads/presign` 直传 → `POST /api/firmwares`」的纯标准库复刻，`docs/PUBLISHING.md`
里有逐字段的 curl 示例。**注意**：服务端字段名是 `descZh` / `descEn`（不是 `desc`），
`category` 必须是平台已有分类 slug（先 `GET /api/categories` 核对）。

