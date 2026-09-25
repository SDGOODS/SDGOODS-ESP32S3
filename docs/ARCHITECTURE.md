# 代码结构与设计要点

写给要改这份代码的人。重点在**为什么这么写** —— 尤其是分层约定与联机同步那部分，
不知道规则就直接改，很容易做出「平台代码被应用污染」或「两边敌机不一样」的 bug。

---

## 1. 运行时总览

```
app_main()                                     main/main.c
 ├─ 日志静音（wifi/bt/phy 等噪音子系统）
 ├─ 电池自锁拉高、sdgoods_lcd_init_panel()             components/sdgoods_board/lcd/
 ├─ sdgoods_wifi / sdgoods_ble / sdgoods_audio / key_input 初始化
 ├─ sdgoods_lvgl_init()  +  sdgoods_touch_init()  +  sdgoods_hw_info_init()
 ├─ apps_register()                            ★ 应用层接线（见 §2）
 ├─ sdgoods_boot_show()                             开机动画（GIF，PSRAM canvas）
 │     └─ 动画结束 → sdgoods_ui_home_create_show() → 应用层建首屏（主页）
 ├─ sdgoods_i18n_init()                             语言：默认英文，设置存 NVS
 ├─ sdgoods_app_shell_init()                        应用外壳（共享音量、顶部下滑菜单）
 └─ sdgoods_lvgl_loop()                           ★ 永不返回：LVGL 唯一心跳
       while (1) {
           sdgoods_power_key_poll();                    电源键（短按返回 / 长按关机）
           lv_timer_handler();                  LVGL 计时器 / 重绘
           sdgoods_apps_poll();                 → 应用层汇总的各应用 *_poll()
           vTaskDelay(2ms);
       }
```

**所有 LVGL API 必须在 `sdgoods_lvgl_loop()` 这个线程里调用。**
其它任务（BLE 回调、音频、串口）只能改标志位，由 `lv_timer` 或页面 `*_poll()` 消费。
游戏 demo（例如 `ui_flappy.c`）的每帧推进逻辑就挂在 `lv_timer` 上作为游戏主循环。

---

## 2. 分层：平台层 ⇄ 应用层（本工程的核心结构）

```
components/sdgoods_board/          平台层（板级支持包，BSP）
    include/   对外接口 —— 应用层只需要 #include "sdgoods_board.h"
    src/       实现：LVGL 移植 / 触摸 / 电源键 / 音频 / 扫描 / 截屏
               + 应用框架 sdgoods_app_shell / 手势返回 / 开机流程 / 钩子注册表
    lcd/       ST77916 QSPI 面板驱动 + 厂商初始化序列
    fonts/     中文子集字体（由 tools/gen_fonts.py 生成）

main/                              应用层
    main.c          装配点
    apps/           应用（用户的代码写这里）
    patches/        对 LVGL 组件的补丁
```

**许可也是按这两层分的**（改代码时别改错）：

| 目录 | 许可 | 说明 |
|---|---|---|
| `components/sdgoods_board/` | Apache-2.0 | 可自由商用、可闭源 |
| `main/`（应用层） | Apache-2.0 | 可自由商用、可闭源 |
| `main/patches/` | MIT | LVGL 衍生 —— **不可更改** |
| `components/sdgoods_board/fonts/` | SIL OFL 1.1 | Noto Sans SC 衍生 —— **不可更改** |

后两处的许可是上游决定的，我们无权追加限制。本仓库其余部分（平台层与应用层）统一以
Apache-2.0 发布。详见根目录 [LICENSING.md](../LICENSING.md)。

新建 `.c` / `.h` / `.py` 之后跑一次 `python3 tools/add_license_headers.py --apply`
补上声明（幂等，已带声明的会跳过）。

### 为什么要有这层划分

平台代码（屏驱动、LVGL 移植、I2S 时序、PSRAM 弹跳预算这些）参数都是**实测调出来的**，
很容易被顺手改坏。把它放进独立组件后，"哪些是底板、哪些是上层"从目录结构上就一目了然，
应用层误碰平台内部实现的机会大幅减少。

### 平台层怎么回调应用层：注册制（`sdgoods_hooks.h`）

平台层**不知道主页长什么样、有哪些应用**。它只认三个回调，由应用层填：

```c
/* main/apps/apps_registry.c */
sdgoods_apps_set_poll(apps_poll);      /* 主循环每轮调用 → 汇总各应用 poll */

static const sdgoods_nav_t nav = {
    .home_create_show = home_create_show,   /* 开机动画结束：建首屏 */
    .home_show        = home_show,          /* 仅切回主页（菜单退出 / 游戏中电源键短按） */
    .apps_show        = apps_show,          /* 切回「应用」启动台 */
};
sdgoods_ui_set_nav(&nav);
```

实现上是一张函数指针表（`src/sdgoods_hooks.c`），**未注册即空操作**，
所以平台层可以独立跑起来（比如只做屏点亮测试）而不会空指针崩溃。

> ⚠️ 硬约束：**平台层不得 `#include` 应用层的头文件**。
> 之前 `sdgoods_lvgl.c` 里塞了 9 个 `ui_*_poll()`、`sdgoods_app_shell.c` 直接调
> `ui_home_show()` —— 那就是"底板反向依赖上层"，任何人换一套 UI 都得改平台代码。
> 现在这条依赖被 `sdgoods_hooks` 反转了。

### 应用清单是一个表

`main/apps/apps_registry.c` 里的 `s_apps[]` 同时驱动两件事：

```c
static const sdgoods_app_t s_apps[] = {
    { "小鸟", ui_flappy_start, ui_flappy_poll },   /* 按钮文字, 进入, 每帧推进 */
    /* …其余应用（主页 / 扫描 / 识别 / 其他）照此注册… */
};
```

- **启动台**（`ui_app_page.c`）遍历它生成圆按钮 —— 所以 `ui_app_page.c` 不需要
  `#include` 任何具体应用，加应用也不用改它；
- **轮询**（`apps_poll()`）遍历它调 `poll`。

按钮位置也是自动算的：1~3 个走单行垂直居中，4~6 个走两行、每行各自水平居中，
超过 `SDG_UI_LAUNCHER_MAX`（6）个不显示按钮（但 `poll` 仍会被调用）。
坐标由 `sdgoods_ui.h` 的 `SDG_UI_BTN_PITCH / SDG_UI_ROW1_Y / SDG_UI_ROW2_Y /
SDG_UI_ROW_MID_Y / SDG_UI_CENTER_X` 推导 —— **增减应用不必手改坐标**。

往这张表和 `CMakeLists.txt` 加应用行是手动的（原 `tools/new_app.py` 已移除）：
在 `apps_registry.c` 的 `s_apps[]` 加一项、在 `CMakeLists.txt` 的 `SRCS` 加一行即可。
要独立开发自己的应用，用 `tools/new_app_project.py` 派生工程（见 BUILD.md / skill sdgoods-new-app）。

---

## 3. 应用框架 `sdgoods_app_shell.c`

新写一个应用页面时，只需要四步：

```c
#include "sdgoods_board.h"                 /* 平台层总入口（含 sdgoods_app_shell） */

lv_obj_t *scr = lv_obj_create(NULL);
/* ...自己画界面... */
lv_scr_load(scr);
sdgoods_app_shell_bind(scr);                        /* 接管手势：下滑出菜单、上滑收起 */
sdgoods_app_shell_set_exit_cb(on_menu_exit);        /* 菜单「退出」→ 回调里清理自己 */
sdgoods_app_shell_set_pause_cb(on_pause);           /* 进菜单时暂停游戏 */
sdgoods_app_shell_set_resume_cb(on_resume);
```

约定：

- 回调名**不要**叫 `on_exit`（与 libc 冲突 → `conflicting types` 编译失败），
  本项目统一用 `on_menu_exit`。
- `bind()` 要在 `lv_scr_load()` **之后**调（它往当前屏挂手势捕获层）。
- 查询菜单是否打开用 `sdgoods_app_shell_menu_is_open()`。
- 退出时先加载目标屏、再释放旧屏资源（`sdgoods_app_shell_leave()`），避免闪屏。
- 界面栅格用 `sdgoods_ui.h` 的 `SDG_UI_*` 常量（3 列 × 2 行 + 标题 + 页脚，
  已避开圆边裁切）。

完整可运行例子：`main/apps/app_template.c`（可直接编译；它没有界面入口，
留在构建里是为了保证模板始终有效）。

---

## 4. 显示链路

- **面板**：`components/sdgoods_board/lcd/` —— ST77916，QSPI，360×360。
  引脚集中在 `include/board_pins.h`，换板子只改这一个文件。
- **LVGL 移植层**：`components/sdgoods_board/src/sdgoods_lvgl.c`
  - 绘制缓冲：**内部 SRAM**，双缓冲 8 行/块，异步 flush。
    ⚠️ 缓冲**不要**回退到 PSRAM：QSPI DMA 的弹跳缓冲预算极小，会出现黑条 / 红线。
  - `LV_COLOR_16_SWAP=y`，16bpp RGB565。
- **LVGL 组件**：`managed_components/lvgl__lvgl`（8.3.11，组件管理器下载，不入库），
  其中 GIF 解码器被本项目打了补丁 —— 见 [`../main/patches/README.md`](../main/patches/README.md)。

## 5. 字体与资源

- 中文用**子集字体**（只含源码里扫到的字符）：
  - `cn_font_14/16.c` —— 全量集（约 970 字），职责是兜底；
  - `si_yuan_black_icon_14/16.c` —— 更小的 UI 精选子集（91 字），
    缺失字由 `.fallback` 编译期落到 `cn_font_*`。
- 字形来源是 **Noto Sans SC**（SIL OFL 1.1），生成文件头部带版权声明（OFL 要求，勿删）。
- 新增文案 → 必须重跑 `tools/gen_fonts.py`，否则出方框；
  用 `tools/gen_fonts.py --check` 可快速校验当前字体是否缺字。
- 换字体 / 改长文案后，先跑 `tools/font_metrics.py` 离线量宽度（会不会超出按钮或圆屏），
  再烧板子截屏确认 —— 设备端串口只有 `'s'` 截屏命令，没有导航命令，
  手势到不了的界面（启动台、菜单）没法用串口翻过去看；而且单张 360×360 截屏
  走 USB-Serial-JTAG 要约 **33 秒**，试错成本很高，务必先在电脑上算。
- ⚠️ **圆屏的「可用宽度」不是常数**，这一点很容易踩：
  屏幕是圆的，同一行文字越靠上/下，能放下的宽度越窄 ——
  `可用宽度 ≈ 2*sqrt(180² - dy²)`（dy = 该行离屏幕中心的最远距离）。
  屏幕中心能放满 360px，而关于页的许可标签在 y=254，只剩约 **291px**。
  所以 `tools/font_metrics.py` 的文案表给每行都带 **y 坐标**，按弦宽自动收紧上限；
  只写死一个 340px 会在这种位置给出「OK」的错误结论（实际两端被圆边切掉）。
- **长文案要限宽折行**，别硬塞一行：

  ```c
  lv_obj_set_width(lbl, 200);
  lv_obj_set_height(lbl, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  ```

  关于页的英文许可标签（`Free for personal use · Commercial needs license`）单行 327px，
  在 y=254 处放不下，就是靠这个折成两行（见 `ui_about.c` 的 `ABT_LICENSE_W`）。
  `font_metrics.py` 会**模拟折行**再逐行判宽，所以文案表里这条要写 `wrap=200`。
- 文案表里凡是「整行文本」都写了 y；圆按钮上的文字不受弦宽限制（按钮自己是圆，按直径 76 判）。
- 开机动画：`src/sdgoods_boot_gif.c` 是一段 GIF 的字节数组（不是解码器），
  由 LVGL 内置 GIF 解码器播放，canvas 在 PSRAM。

## 6. 串口截屏

`components/sdgoods_board/src/sdgoods_screenshot.c` 用 LVGL 的 `lv_snapshot` 抓当前屏幕 →
在板端用 vendored 的 `jpegenc` 组件把 RGB565 **直接编码成 JPEG**（典型 20~50KB）→ 以
二进制 blob 经串口发出；电脑端 `tools/screenshot_recv.py` 按 BEGIN 头里的 `bytes` 字段
精确收齐、直接落盘为 .jpg（头里 `fmt=1`）。`fmt=0` 时是原始 RGB565，PC 端转 PNG（兜底）。

```bash
python3 tools/screenshot_recv.py -p /dev/cu.usbmodemXXXX -o shot.png -n 1 -t
```

设备侧触发：**串口直接发 `s`**（菜单里的截屏按钮已移除；脚本加 `-t` 会自动发）。

实现上有两个坑值得知道：

1. **次级 console 不注册 stdin**。本项目 `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` 且
   `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`，USB-Serial-JTAG 只复刻输出，
   用 VFS 读不到输入。所以触发字符 `'s'` 是用
   `hal/usb_serial_jtag_ll.h` 的 `usb_serial_jtag_ll_read_rxfifo()` 直接在任务里轮询 RX FIFO 拿到的。
2. **传输期间要静音日志**（`esp_log_level_set("*", ESP_LOG_NONE)`），否则日志会插进数据流。
3. **像素是原始二进制、不靠换行分帧**：二进制里可能含 `0x0A/0x0D`，所以接收端
   **不能**用 `readline()` 按行解析。正确做法是先按文本找 `===SHOT-BEGIN===` 头，
   再依据头里的 `bytes` 字段「精确读取那么多字节」作为图像，最后用 `===SHOT-END===`
   收尾校验（见 `tools/screenshot_recv.py` 的 `ShotAssembler`）。早期版本用 base64 编码
   后再按行分帧，既膨胀 33% 又容易因半行被当成整行而静默截短，已废弃。

**耗时**：板端先把 360×360 截屏用 JPEG 编码（典型 20~50KB），经控制台 fwrite 写出，
走 USB-Serial-JTAG **约 2~5 秒**（链路上限约 10.5 KB/s，是物理瓶颈；试过直接写 TX FIFO
反而更慢）。JPEG 缓冲分配失败时会自动退回原始 RGB565（259KB，约 25 秒）。设备端在抓帧后
会弹一个「截图中…/Capturing…」浮层 + 进度条，截屏期间屏幕明显在动，不会让人以为是卡死。

### 怎么拍到"手势才到得了"的界面

启动台、关于页、外壳菜单这些要点触摸/滑动才能到的界面，串口没有导航命令过不去。
需要自动化核对时，加一个**临时探针**：

1. 在 `apps_registry.c` 里加 `probe_step(int n)`，把请求写进一个 `volatile` 变量，
   真正的切页动作放在 `apps_poll()`（LVGL 线程）里消费；
2. 在 `sdgoods_screenshot.c` 的串口接收分支里，把数字字符 `'1'~'9'` 接到 `probe_step()`。

这样电脑端发 `1` 切页、发 `s` 抓图，节奏完全受控（不受定时漂移影响）。
⚠️ **探针是诊断代码，验证完必须删干净**（临时改动不要提交）。

## 7. 界面语言（中 / 英）

`components/sdgoods_board/src/sdgoods_i18n.c` + `include/sdgoods_i18n.h`。

- **出厂默认英文**；用户在 DEMO 页最后一颗按钮（显示当前语言）切换。
- 选择写 NVS（命名空间 `sdgoods` / 键 `lang`），重启保留；NVS 不可用时只影响持久化。
- 取词是**内联**的，没有字符串表：

  ```c
  lv_label_set_text(title, SDG_T("关于", "About"));
  snprintf(buf, sizeof(buf), SDG_T("固件版本 %s", "Firmware %s"), BUILD_VERSION_STR);
  ```

  中英写在同一行，改文案不会漏掉另一种语言；英文属于 ASCII，不需要动字体子集。

- **品牌名与公司名不翻译**（谷仓共创计划 / 谷仓 SDGOODS 开放平台 / 谷仓次元屏 /
  深圳希德创新网络有限公司）：它们有商标与法定名称属性，两种语言下都按原样显示。
- 例外说明：主页顶部的产品名写成 `SDG_T("谷仓次元屏", "SDGOODS E-BADGE")` ——
  这**不是翻译**，而是产品在英文语境下的**官方名称**（E-BADGE = Electronic Badge）。
  品牌标识类文案要改，先看 `TRADEMARK.md`。
- 联系方式（`SDGOODS_CONTACT_EMAIL`）**只在开机串口横幅里打印一次**：
  只拿到一颗烧好的芯片也能找到源头。**关于页不展示邮箱**——按产品口径那里只显示
  官网 `SDGOODS_HOMEPAGE`。宏定义在 `main/gen_build_version.cmake`，
  不要在别处另写邮箱字面量。

### 切换语言后界面怎么更新

LVGL 屏一旦建好，label 上的文字不会自己变。所以：

- **主页**（唯一常驻屏，`ui_home_create()`）语言变了就整体重建；
- **DEMO 页**的语言按钮在回调里就地重建本页（用户立刻看到效果）；
- 其它页（启动台 / 关于 / 三个子页 / 游戏）关闭时本就销毁屏幕，下次进入自然用新语言；
  带屏缓存的页面仍会比对 `sdg_i18n_seq()`，发现语言变过就先删屏重建 ——
  三行代码，别省：

  ```c
  static uint32_t s_lang_seq;
  if (s_scr && s_lang_seq != sdg_i18n_seq()) { lv_obj_del(s_scr); s_scr = NULL; }
  if (s_scr) { lv_scr_load(s_scr); return; }
  s_lang_seq = sdg_i18n_seq();
  ```

### 一个必须遵守的约定：回调不要拿按钮文字当键

按钮回调原来用 `strcmp(name, "关于")` 分派，切到英文后按钮文字变 `"About"`，
`strcmp` 就对不上了。现在统一传**语言无关的键**：

```c
make_round_btn(s_scr, x, y, SDG_T("关于", "About"), "about");   /* 第四参 = 键 */
static void on_demo_btn(lv_event_t *e) {
    const char *key = (const char *)lv_event_get_user_data(e);
    if (strcmp(key, "about") == 0) { ui_about_page_show(); }
}
```

启动台的像素图标同理：`sdgoods_app_t` 里的 `icon` 字段（`"bird"` / `"scan"`）是键，
`label_zh` / `label_en` 是显示文字 —— 两者不要混用。
