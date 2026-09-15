# AGENTS.md —— 给 AI 编程助手的开发约定

> 本文件是写给 **AI 编程助手**（Claude Code / Cursor / Copilot / Codex 等）看的项目说明书。
> 你在为本仓库改代码前，请先读完本文件；人也可以读，但重点在"约束"。
>
> 目标读者是**谷仓 SDGOODS 开放平台的用户**：他们用 AI 辅助，在这块圆屏设备上做二次开发。

---

## 0. 一句话说清项目

ESP32-S3 + 360×360 圆屏的桌面设备固件。ESP-IDF 5.5 + LVGL 8.3.11。
所有 UI 与游戏逻辑都在 `main/` 下，纯 C，没有第三方 UI 框架。

**硬件参数不可改**（除非用户明确说换了板子）：见 `main/board/board_pins.h`。
引脚、屏幕型号（ST77916 QSPI 360×360）、PSRAM 类型（8 线 Octal）都是固定的。

---

## 1. 编译与烧录（照抄，不要自创命令）

```bash
# 编译（首次会自动下载 LVGL，并自动应用 main/patches 里的补丁）
idf.py build

# 烧录 + 看日志
idf.py -p <串口> flash monitor
```

⚠️ **有几个坑，务必按下面的方式做：**

1. **每次改完必须真编译**（`idf.py build`），不要凭"看起来对"就说改完了。
   `-Werror=all` 是开着的，警告会直接变成错误。
2. **改了 `main/` 下的源码后，如果用户的工作副本是「编辑目录 / 构建目录分离」的**，
   需要先把 `main/` 同步到构建目录再编译 —— 具体路径问用户，不要猜。
3. **不要 `rm -rf build`**。要么让 ninja 增量编译，要么换一个新的 `-B build_xxx` 目录。
4. 串口号会变（S3 原生 USB-Serial-JTAG 的编号与芯片 MAC 绑定），**不要把串口写死**，
   先 `ls /dev/cu.usbmodem*` 或 `ls /dev/ttyACM*` 看当前是什么。
5. **不要改 `managed_components/`**。它是组件管理器下载的，会被重新覆盖；
   要改 LVGL 就改 `main/patches/` 下的补丁文件（见第 5 节）。

---

## 2. 加一个新应用页面（标准姿势）

`ui_app_shell` 负责统一交互（顶部下滑菜单、退出、暂停）。新页面只要四步：

```c
#include "ui_app_shell.h"

static void on_menu_exit(void) { /* 释放自己的资源，回到主页 */ }
static void on_pause(void) {}
static void on_resume(void) {}

lv_obj_t *scr = lv_obj_create(NULL);
/* ...画你的界面... */
ui_app_shell_bind(scr);
ui_app_shell_set_exit_cb(on_menu_exit);          /* ⚠️ 回调别叫 on_exit，与 libc 冲突 */
ui_app_shell_set_pause_cb(on_pause);
ui_app_shell_set_resume_cb(on_resume);
lv_scr_load(scr);
```

然后在 `main/CMakeLists.txt` 的 `SRCS` 里加上你的 `.c` 文件。

参考实现：`ui_snake.c`（最简单）、`ui_plane.c`（最复杂，带游戏循环和联机）。

**LVGL 是单线程的**：所有 LVGL API 只能在 LVGL 主循环
（`main/lvgl_port.c` 的 `lvgl_port_loop()`）里调用。其它任务（BLE 回调、音频、串口）
只能改标志位，由 `lv_timer` 或页面自己的 `*_poll()` 消费。

---

## 3. 改了中文文案 → 必须重新生成字体（最容易忘的一步）

中文用的是**子集字体**（只包含源码里出现过的字），所以新增任何汉字后不重新生成，
屏幕上就是**方框**：

```bash
npm i lv_font_conv
python3 tools/gen_fonts.py --bin ./node_modules/.bin/lv_font_conv --font <字体路径>
```

脚本会扫描 `main/` 全部字符重新生成 `cn_font_14.c` / `cn_font_16.c`，
并逐字校验有没有漏字（漏了会报错退出）。

> 字体源请用**授权允许再分发**的字体（SIL OFL 1.1 的思源黑体 / Noto Sans SC）。

---

## 4. 内存：这块芯片最容易踩的坑

- **内部 SRAM 很小**（约 400KB 可用），**PSRAM 有 8MB**。
- 大缓冲（GIF canvas、大图片、大数组）必须走 PSRAM：
  `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
- **但 LVGL 的绘制缓冲不能放 PSRAM**（QSPI DMA 弹跳缓冲预算极小 → 黑条 / 红线）。
  这块已经调好，别动 `lvgl_port.c` 里的缓冲配置。
- AI 常见错误：一次性创建几百个 LVGL 对象 / 大数组开在栈上 →
  运行时报 `NO_MEM` 或直接重启。加功能时优先"复用对象 + 固定大小数组"。

---

## 5. 不要直接改 `managed_components/`

LVGL 的 GIF 解码器被本项目改过（canvas 走 PSRAM、RGB565、源数据拷进 RAM），
改动**横跨 3 个文件**（`gifdec.c` / `gifdec.h` / `lv_gif.c`），必须成套替换。

- 补丁文件在 `main/patches/lvgl-8.3.11-gif-psram/`，随源码提交；
- `main/CMakeLists.txt` 在 CMake configure 阶段自动调用
  `main/patches/apply_lvgl_patches.py` 覆盖过去，并**逐字节校验**；
- 只改一半的症状：`too few arguments to function 'gd_open_gif_data'` 编译失败，
  或开机动画颜色发花。

要升级 LVGL 版本，见 `main/patches/README.md` 的升级步骤。

---

## 6. 改飞机游戏的联机逻辑前，先读这四条铁律

（详见 `docs/ARCHITECTURE.md`，**违反任意一条都会让两台设备的敌机分叉**）

1. 世界生成必须走共享 PRNG，且**消费次数固定**（槽满也要消费）；
2. 本地私有行为不能喂共享 PRNG；改变子弹模式必须扩协议字段让对端同公式复制；
3. 会改变敌机集的瞬时事件（如炸弹清屏）必须广播事件序号；
4. **玩家受击不能 despawn 敌机/敌弹** —— 改扣命 + 无敌闪烁。

另外，「同一 tick 的语句顺序」也有硬约束：

```
道具拾取 → 火力倒计时/到期 → 自动开火 → …… → 广播状态包
```

---

## 7. 改完 UI 怎么自己验证（强烈建议）

设备支持**串口一键截屏**，这是 AI 自证 UI 改动的最有效手段 —— 别只靠读代码：

```bash
python3 tools/screenshot_recv.py -p <串口> -o /tmp/shot.png -n 1 -t
# -t: 每 5 秒重发一次触发，收到就停
```

然后**打开 PNG 看一眼**（AI 可以直接看图）：中文有没有方框、控件有没有重叠、
颜色对不对。这一步比读十遍代码有用。

游戏类改动可以用 `tools/plane_stat.py` 边玩边统计道具掉落/档位/受击次数。

---

## 8. 提交前检查清单

- [ ] `idf.py build` 真的过了（不是"应该能过"）
- [ ] 新增中文字符已重跑 `tools/gen_fonts.py`，且校验零缺字
- [ ] 没有把大数组/大缓冲放到栈上或内部 SRAM
- [ ] 改玩法的话：联机四条铁律 + same-tick 顺序都遵守了
- [ ] `board_pins.h`、`lvgl_port.c` 的缓冲配置没被顺手改掉
- [ ] 截屏验证过画面（UI 类改动）
