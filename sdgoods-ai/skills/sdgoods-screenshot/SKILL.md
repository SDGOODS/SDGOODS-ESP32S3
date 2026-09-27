---
name: sdgoods-screenshot
description: ESP32「一键截屏」接收端 + 「合成触摸」手势验证，把谷仓次元屏经 USB 串口发的原始位图还原成 PNG，给 AI「看图」验证 UI/字体/画面；也可用临时替换 indev read_cb 的方式在无人手条件下验证点按/滑动判别。封装 tools/screenshot_recv.py（-t 自动触发、-p 端口、-n 张数、--pre 先切页、--caps 能力探测、--selftest 自检）。Use when asked to screenshot / 截屏 / capture the badge screen / verify UI visually / 验证触摸手势 / 滑动误触。
agent_created: true
---

# SDGOODS 串口截屏

## 何时使用
- 验证 UI 改动、字体排版、画面是否正确（尤其手指够不到的启动台/菜单界面）。
- 单张约 2~5 秒（JPEG 编码）或约 25 秒（旧固件回退 RGB565）；比烧板子试错便宜得多。

## ⚠️ 最常见的失败：忘了加 `-t`

`screenshot_recv.py` **默认只是「监听等设备发图」**，不会自己发 `'s'`。
⚠️ 脚本位置：在**谷仓工程仓库内**（如 `SDGOODS-ESP32S3/tools/screenshot_recv.py`、派生工程 `<APP>/tools/screenshot_recv.py`），**不在本 skill 目录**（skill 只封装文档）。
不加 `-t`（或 `--pre`）就跑 ⇒ 老老实实等满 120 秒然后
`等待超时：120 秒内没有收到完整截屏` —— 很容易被误判成「设备没在跑 / 截屏链路坏了」，
其实设备好好地在跑。要主动触发**必须**带 `-t`：

```bash
python3 tools/screenshot_recv.py -p /dev/cu.usbmodem21301 -t -o shot.jpg
```
（超时第一排查顺序：① 有没有 `-t` ② 设备是否在跑应用，再去看链路。）

## 运行（从仓库根目录）
> ⚠️ **脚本只在 `SDGOODS-ESP32S3/tools/` 里有一份**（2026-09-21 核实：`SDGOODS_LAUNCHER/tools/` 与
> `SDGOODS-HELLO/tools/` 都**没有**这个脚本）。它跟被截的是哪个工程的固件**无关** —— 不管当前板子上跑的是
> 启动器还是某个 app，一律 `cd <工作区>/SDGOODS-ESP32S3` 再跑。别去 skill 目录下找 `tools/`（那里只有 SKILL.md）。

```bash
# 自动向串口发 's' 触发并接收 1 张，存 shot_时间戳.png
python3 tools/screenshot_recv.py -t

# 指定端口 / 连续抓 3 张 / 指定输出
python3 tools/screenshot_recv.py -p /dev/cu.usbmodem21301 -n 3 -o my_shot.png

# 探测固件能力（串口发 '?'，看是否含 SHOT）
python3 tools/screenshot_recv.py --caps

# 不需要设备，自检解析 + PNG 写出
python3 tools/screenshot_recv.py --selftest
```
- 协议：`===SHOT-BEGIN w=360 h=360 ...===` 文本头 + 定长原始二进制 + `===SHOT-END===`；
  `fmt=1` 为 JPEG（PC 直接落 .jpg），`fmt=0`/无字段为 RGB565（PC 转 PNG）。
- 需要 pyserial：用带 pyserial 的解释器（系统 python3 可能无）。脚本会提示可用路径。

## AI 处理策略
- 接收前**先让用户关掉** `idf.py monitor` / 串口助手 / 浏览器 Web Serial 页面——
  它们占住同一串口会导致打不开或数据串台。
- ⚠️ **幻触**：串口一打开，屏上就可能凭空出现点/划，曾多次被误判成固件 bug。典型表现：
  ① 凭空拉出控制中心（截回来的是 CC 而不是主页）；② 横向拖动主页图标行（像「被滑过一格」）；
  ③ 幻触点按触发 `launch slot N` 把设备切进某个 app。
  对策：打开串口后立刻 `ser.rts = ser.dtr = False`，**能减轻但不能根除**
  ⇒ 判图前先确认「这一屏是不是我要的那一屏」。
  （注：USB-Serial-JTAG 的 DTR/RTS 并**不**接到 CHIP_PU，所以这两种电平都不会复位芯片——
   仓库里的 `tools/screenshot_recv.py` 反而把两者置 `True` 且工作正常。别把幻触归因成
   「电平设错导致复位」。真要复位芯片用 **DTR 脉冲**：`setDTR(False); sleep(0.05);
   setDTR(True); sleep(0.1); setDTR(False)`；**RTS 脉冲不复位**。）
- 若被幻触切进了某个 app，写回空白 otadata 即可回启动器：
  `esptool --chip esp32s3 -p <port> write_flash 0x310000 build_xxx/ota_data_initial.bin`
- 把生成的 PNG 用 Read 工具打开「看一眼」，核对布局/文字/颜色；发现方框即字体缺字（见 `sdgoods-fonts`）。
- 真机不在手边：用 `--selftest` 验证解析管线本身无问题，再请用户上机补真实截屏。

## 把 UI 差异「量化」出来，别只靠肉眼（2026-09-21 补）

截回来一张图、肉眼看一眼就下结论，很容易翻车（360² 的小图 + JPEG 压缩 + 抗锯齿，
「少了一条 4px 的灰条」「图标偏了 2px」这类根本看不出来；也容易把封面图**自身**的灰内容
看成 UI 缺陷）。可靠做法是把画面当数组，先问一个**能用数字回答的问题**：

```python
import numpy as np
from PIL import Image
a = np.asarray(Image.open(shot).convert('RGB'), dtype=np.int16)
r,g,b = a[:,:,0],a[:,:,1],a[:,:,2]
gray = (np.abs(r-g)<=14)&(np.abs(g-b)<=14)&(r>=45)&(r<=220)   # 阈值按主题微调
# 例：找「细长条」= 最长连续灰 run（竖条按列扫、横条按行扫），修前修后对比数值
```

可复用的三招：
1. **找细长条 / 边框 / 滚动条**：按行/列扫最长连续 run。修前 49px → 修后 10px 就是「条没」，比肉眼可靠。
2. **判断「位置有没有变」**：在被改对象的**内部区域**（避开外圈，外圈颜色可能是随机的）做 ±6px 平移互相关，
   最佳对齐若在 `dx=dy=0` 且平均差 < 1 灰度级 ⇒ 位置没动。
3. **连通域 + 形态过滤**：`n≥40` 的连通域里挑 `(w≥3 且 h≥40)` 或 `(h≤4 且 w≥40)` 的，就是条状物。

⚠️ 反面教训：**先确认「这个像素属于 UI 还是属于内容」**。封面图是 AI 生成的像素画，
本身就带灰色描边/文字 ⇒ 灰像素扫描一定会命中它们。所以定位时必须结合
「哪些图标有、哪些没有」做**对照**（这次就是靠「只有贴了封面的图标有灰条、字母回退态没有」锁定根因的）。

### 量化判据本身要先用「几何反推」自检一次（2026-09-27 血泪）

**症状**：固件没问题，判据把**容差问题**报成 🔴，于是白折腾一轮（一晚连踩 4 次）。

**铁律**：写完判据后，先拿**设计常量**反推一遍「屏幕上应该看到什么」，再跟脚本口径对齐。
对不上就是脚本错，不是固件错。**别急着去改固件。**

四个实例（都发生在首页顶部状态条核验里）：

| 假 🔴 | 真相 |
|---|---|
| 凸点量到 4×7，判据要 4×9 | 凸点带 `radius=2` 圆角 + JPEG 弱化最外 1px ⇒ 可见核心只有 4×7。判据改「4 宽、7~9 高」。 |
| 「solo 时徽标区域为空」失败 | 检测区写成 `x128..170`，而 solo 的电池框左沿**正好 155** 落进区内 ⇒ 量到的是框自己。solo 专用区右界必须 <155。 |
| 徽标量到 16×13，判据要 24×17 | 24×17 是**容器**尺寸；图形（底点+三弧）居中于容器只占 16×13。反推：最外弧 r=11、弧心在容器 (12,12) ⇒ 屏幕 x 143.6..160.4，与实测 144..159 吻合 ⇒ 渲染正确。 |
| 开关滑块量到 73 宽 | OFF 态滑块色 `#8E8E93` 与「OFF」**文字同色**，bbox 把两者合并了。 |

### 颜色撞色 / 相邻部件混入 ⇒ 用 8-连通域，别用整块 bbox（2026-09-27 补）

`bbox_of(区域, 颜色)` 在下面三种情形**必然**报错 —— 改用**连通域**：

- 同色部件相邻（灰开关 `#3A3A3C` vs 灰 Scan 按钮；滑块的 `#8E8E93` vs OFF 文字）；
- 目标部件贴着另一个同色部件（电池框左沿与徽标检测区重叠）；
- 区域把背景/遮罩的近似色一并框入（弹窗面板 `#1C1C1E` vs 遮罩 ⇒ 容差要从 14 收到 10）。

现成实现（可直接抄）：

```python
def components(im, region, target, tol, min_area=6):
    """8-连通域分离：返回 [(x0,y0,x1,y1,area)]，按面积降序。"""
    x0, y0, x1, y1 = region
    W, H = x1 - x0, y1 - y0
    mask = bytearray(W * H)
    for yy in range(H):
        for xx in range(W):
            if near(px(im, x0 + xx, y0 + yy), target, tol):
                mask[yy * W + xx] = 1
    seen = bytearray(W * H)
    out = []
    for idx in range(W * H):
        if mask[idx] and not seen[idx]:
            stack = [idx]; seen[idx] = 1
            xs, ys = [], []
            while stack:
                i = stack.pop(); cy, cx = divmod(i, W)
                xs.append(x0 + cx); ys.append(y0 + cy)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            j = ny * W + nx
                            if mask[j] and not seen[j]:
                                seen[j] = 1; stack.append(j)
            if len(xs) >= min_area:
                out.append((min(xs), min(ys), max(xs), max(ys), len(xs)))
    out.sort(key=lambda b: -b[4])
    return out
```

用法：先按**面积降序**取最大域（主体），再按特征挑次域 —— 例：凸点 =「高 ≥6 的次大域」，
滑块 =「宽高差 ≤3 且宽 ≥18 的圆」。

⚠️ 容差要卡在「同色部件能分开、JPEG 抖动又不丢」的窗口：本次电池框用 `tol=22`
（1px 缝的过渡像素 `|0x70-0xE0|=112` 一定不匹配；放到 `tol=30` 就开始有合并风险）。

### 定位第一步：先把圆心**量**出来，别估（2026-09-21 连踩三次）

要给某个圆形 UI 元素做半径/角度分析，第一件事是拿到它的圆心。这一步估错，
后面所有数字全是垃圾，而且**错得看不出来**（会输出一堆自洽的荒谬值）。三种典型的错法：

1. **肉眼看截图估圆心** —— 360² 的小图在脑子里换算成坐标，我目测出 y=228，
   实测是 **y=180**，差了 48px。一眼扫出来的绝对值不可信。
2. **把掩码切在 `x >= w/2`** —— 主页第 1 个图标圆心**正好在 x=180**（屏宽一半），
   这一刀等于只留半张圆盘，质心被拉到 x=155。判据要写成「只在某个 ROI 内」，
   不是「只取左半边」。
3. **用宽亮度窗抓「灰色」** —— `lum in [70,160]` 会把标题/电量的**抗锯齿灰边**
   一起吃进来，把质心拉出圆盘外。灰底回退态的圆盘是很均匀的一个值（实测 lum≈110），
   窗口收到 ±10 就够，噪声全排除。

**最稳的顺序（照抄即可）**：
```python
# ① 先把掩码打成缩略 ASCII 图，肉眼确认形状与包围盒 —— 这一步比任何自动定位都快
for by in range(0, 360, 6):
    print(''.join('█' if m[by:by+6, bx:bx+6].mean() > .9 else
                  ('#' if m[by:by+6, bx:bx+6].mean() > .6 else
                  ('◇' if m[by:by+6, bx:bx+6].mean() > .3 else ' '))
                  for bx in range(0, 360, 6)))
# ② 从 ASCII 图读出包围盒，在该 ROI 内取「均匀灰」像素求质心（别切半边）
# ③ 用「外缘半径 ≈ 60」自证：偏离就说明 ROI 里混进了别的东西
```
**主页图标行的已知几何**（可直接拿来当基准，省掉第 ①② 步）：
圆盘直径 120（`ICON`）、间距 22（`GAP`）、第 i 个圆心 x = `180 + i*142`（i=0 时 x=180），
**行圆心 y = 180**（屏幕正中）；弧光半径 ≈ 48（`hl_size = ICON*84/100 = 100`，弧从 r=50 向内 4px）。

**带封面的图标不能用来量弧光**：封面是像素画，本身就有亮/灰像素，会把半径剖面和角度直方图
全糊掉。要量圆盘上的装饰（弧光、描边），先把该槽的封面块**备份后擦掉**进回退态
（`read_flash <槽尾地址> 0xC000` → `erase_region` 同区间 → 量完 `write_flash` 写回）：
回退态下圆盘是纯灰 + 两个白字母，信号干净。**擦之前一定先备份**。

**「端点圆帽 vs 平切」这类 3~4px 的差异，量化窗口不能开太大**：弧宽 4px、半径 50px 时，
圆帽只往端点外多涂约 2px（≈2.3°）。用「端点 ±3° 内亮像素占比」这种窗口，
一半窗口落在弧外 ⇒ 真正的平切也会显示约 50%，看起来像「有圆帽」。
更干净的判据是**看端点角度之外有没有亮像素**，以及**同角度下端点区 vs 弧中段的占比之比**。
结论：这种量级的差异本来就接近视觉阈值，别用数字去"证明"用户没看见的东西，
要如实说明「这一项只值 2px，主要观感变化来自别处」。

### 拼「改前 / 改后」对照图给用户看

`PIL` 拼图 + 放大（`Image.LANCZOS`，2× 看整体、5× 看 3~4px 的细节）比贴两张原图有用得多。
**中文标注的字体坑**：本机 **没有 `/System/Library/Fonts/PingFang.ttc`**（写了它不会报错，
PIL 静默退回位图字体 ⇒ 中文全成方框「□□」）。可用的是：
`/System/Library/Fonts/Hiragino Sans GB.ttc`、`/System/Library/Fonts/STHeiti Light.ttc`、
`/Library/Fonts/Arial Unicode.ttf`。写完先 `getmask('弧').size` 自证能渲染再拼图。

## 能力探测
- 网页端「从设备截图」会先发 `?` 探测 `SDGOODS-CAPS:SHOT`；本机 `--caps` 等价。
- 若缺 `SHOT`，提示用户在 BSP 启用 `CONFIG_SDGOODS_SCREENSHOT` 重编（见 docs/PUBLISHING.md）。

## 截「非首屏」界面：先发字符切页，再发 's'
截屏只能拿到**当前显示的那一屏**。要核验控制中心 / 二级页这类需要触摸才能到的界面，
必须在 `main/launcher_main.c` 的 `sdgoods_console_ext_cmd()`（弱符号覆盖点）里挂调试命令，
然后用「先发命令字符、等它渲染完、再发 's'」的顺序截屏：

```bash
# --pre <char>：先发该字符，等 1.5s 让页面渲染，再发 's' 抓图
python3 tools/screenshot_recv.py -p /dev/cu.usbmodem21301 --pre c -o cc.png
```
启动器现有调试命令（均为**串口专用**，不进正常交互流程），对应 `sdgoods_launcher_cc_debug_open(which)`：
- `c` → 控制中心一级页（等价「主页顶部下滑」）｜`which=0`
- `d` → 控制中心 + 数据二级页（`Slot x / y`、`RAM Free a / b KB`、`MEM Free c.d MB`）｜`which=1`
- `b` → 控制中心 + 电量二级页（`Voltage x.xxV` + `Level nn%`）｜`which=2`
- `l` → 启动 slot 0（会重启）

⚠️ 调试钩子内部**先 `cc_close()` 再 `cc_open()`**。否则屏上已开着 CC/二级页时，`cc_open()` 会因
`s_cc != NULL` 直接 return，你截回来的仍是**上一次的二级页**，很容易误判成「布局改动没生效」。
（板子长期停在什么页面取决于幻触，见下。）

⚠️ 这些命令的回调跑在 console RX 任务里，**绝不能直接建 LVGL 对象**：
必须 `lv_async_call()` 转到 LVGL 线程执行，否则对象树/事件链在错误线程被改 → 崩溃。

## 「无人手」验证触摸手势：合成触摸序列

串口截屏只能看画面，**看不到手势判别走了哪条分支**（点按 / 滑动 / 误触）。人手又够不到屏幕。
解法：在固件里挂一个调试钩子，**临时替换触摸 indev 的 `read_cb`**，喂进一段「按下 → 逐帧移动 → 松开」的序列，
跑完立刻还原原回调。对 LVGL 而言与真手指**完全等价**，判别分支则从串口日志直接读出来。

启动器已内置（`main/ui_launcher.c` 的 `ui_launcher_debug_synth_touch(kind)` + `main/ui_launcher.h` 声明）：

```bash
python3 tools/screenshot_recv.py -p <port> --pre 2   # 合成「从上往下滑」
# 再看串口日志里的判别结果，例如：
#   slot 0: swipe down 50 px (dx=0) -> open control center
#   slot 0: tap (max move 0 px of 24) -> launch
```

### 实现位置：平台层（全设备一份），启动器只是薄包装
同一套实现在 `components/sdgoods_board/src/sdgoods_tap.c`，**全设备一份**：

```c
void sdgoods_tap_synth(int x, int y, int dx, int dy, int frames);      /* 任意任务 */
void sdgoods_tap_synth_now(int x, int y, int dx, int dy, int frames);  /* 须在 LVGL 线程 */
```
- 平台弱默认 `sdgoods_console_ext_cmd` 已把 **`'0'`..`'5'`** 接上 ⇒ **任何 app 零调试代码**
  即可验证手势：`0`=点下排按钮位(180,218)｜`1`=点中心｜`2`=下划(开控制中心)｜`3`=上划｜`4`=右划｜`5`=上划(底部起)。
- 启动器里那份是**薄包装**：只额外做「起手点固定 `(70,157)`」+「每帧前 `lv_obj_scroll_to_x(s_row, 0, LV_ANIM_OFF)` 归零」两件启动器特有的事。
- 实现要点（换任何 LVGL 项目都通用）：`lv_indev_t.driver`、`driver->read_cb`、
  `lv_indev_data_t{point,state,continue_reading}` 都是**公开字段**；`lv_indev_get_next(NULL)`
  取触摸设备；`LV_INDEV_STATE_PRESSED/RELEASED`。序列放完用帧计数器自动还原原 `read_cb`。
- ⚠️ **日志 tag 是 `gesture`，不能是 `tap`**：tag 取 `tap` 会打出 `tap: synth: ...`，
  含子串 `"tap: "`，与「按钮被点」的 `cc: tap: Volume` 撞车 ⇒ 做禁词校验的回归脚本会把
  合成触摸本身误判成「按钮被误触」（曾制造 9/23 假阳性）。禁词要写 `"cc: tap: "`（带完整 tag）。
- 手势矩阵回归（23 步，must/mustnot 断言）见技能 `sdgoods-platform-sync`。
- ⚠️ 钩子必须只做 `lv_async_call(...)` 转到 LVGL 线程：console RX 是独立 FreeRTOS 任务，
  直接改 LVGL 对象 / indev 驱动会跨线程出问题。
- 测完若被切进某个 app，写回空白 otadata 回启动器即可（见上）。
- 用途举例：证明「点按进 app」没被滑动判别逻辑误伤（回归探针），以及横滑确被 LVGL 原生抑制（无需自己拦）。

## 复现「开机首屏」而不重启：用 `'3'` 把主页图标行滚回最左端（2026-09-21 补）

**幻触会把主页图标行横向拖走**（每次开串口都有概率，实测这次开机就被拖到第 3 个图标居中），
于是你截到的根本不是开机态，却会以为「开机第 1 个居中的改动没生效」。

不用重启、也不用 DTR 脉冲（它并非每次生效）——**发 `'3'`**：
启动器的合成触摸包装在每帧前会先 `lv_obj_scroll_to_x(s_row, 0, LV_ANIM_OFF)` 归零，
而 kind=2（右划）在**已经处于左边缘**时不会真的滚动（`sl == 0` ⇒ 无滚动对象），
所以跑完恰好停在 `scroll == 0`，即「第 1 个图标居中」的开机态，且**不会**误触进 app。
（kind=0 是原地按下松开，会真的启动居中那个 app，别用。）

```bash
python3 tools/screenshot_recv.py -p <port> --pre 3 -o home_leftmost.png   # 开机态（第 1 个居中）
```

同理，要看「滚动中的样子」就用 `'5'`（kind=4，左划，会真滚动）。

## 触发「图标行真的换一个居中」：用 `'6'`-`'9'`（launcher 专有），且必须向左拖（2026-09-27 补）

启动器 `main/launcher_main.c` 的 console 分支把 **`'6'`-`'9'`** 接到 `ui_launcher_debug_synth_gesture(…)`，
用来「换一个居中图标」——但 🔴 **它们全是 dx=+25 的「向右」拖**（`'6'`:(70,180,+25,0)、`'7'`:(60,180,dy)、
`'8'`:(200,180,+25)、`'9'`:(276,118)），在首屏**完全没用**：

1. LVGL 只在「内容溢出的方向」滚（`lv_indev_scroll.c` 判 `(sl>0 || sr>0)`，第一行 `scroll==0` 时向右无内容可露）
   ⇒ 右拖直接判为「返回/无滚动对象」。
2. 即使方向对，**图标节距 150px**，`'6'`/`'7'` 的 25px 会被 `SNAP_CENTER` 弹回原位 ⇒
   **三张截图 md5 一模一样，极易误判成「改的代码没生效」**。

正确做法：临时把某个键改成**向左 ≥150px** 再编译验证（例如 `case '8': …(200,180,-160,0,3);`），
验证完**必须还原并重编**。判定标准：home→sw1 的**中心 132² 区域像素要变**（换图标了），
且主题色圆环**仍精确居中**（环跟着新居中图标走）；本机若只装 2 个 app 则 sw2==sw3（滑不动），属正常。

## 截屏超时的第一嫌疑：设备根本没在跑

`--after no_reset` 是**读操作**的推荐用法（见 skill `sdgoods-build-flash` 的 verify/read 章节），
但它会让芯片**停在 bootloader 里**。紧接着去 `screenshot_recv.py` 必然只等到超时：

```
等待超时：120 秒内没有收到完整截屏
```

**别去查截屏链路**（脚本、波特率、SHOT 能力位都没问题）。先补一次会让芯片跑起来的复位：

```bash
$IDFPY -m esptool --chip esp32s3 -p <port> --after hard_reset flash_id   # 只为复位
sleep 15   # 等开机动画过完（开机动画期间 SHOT 能力位还没注册）
```

诊断顺序写死在习惯里：**先问「设备在跑应用吗」，再问「截屏链路通吗」。**

## 端口会变（MAC 绑定）
`/dev/cu.usbmodem*` 的编号按设备 MAC 分配，同一台机器上换固件/换口都会变（见过 21101 / 21201 / 21301）。
每次都用 `ls /dev/cu.usbmodem*` 现查，别沿用上次的号。
⚠️ 设备刚复位/正被 esptool 占用时开串口会抛 `OSError: [Errno 6] Device not configured`。
这不是链路坏了：**关掉 esptool、等设备自己跑起来再重试**即可（2026-09-27 踩过一次）。

## 圆形屏幕的「圆内布局」怎么验：数值法，不靠肉眼（2026-09-27 补）

360×360 圆屏上，任何新 UI（尤其虚拟键盘这种满屏控件）都必须全部落在内切圆里，
判据是**外角点到圆心 (180,180) 的欧氏距离 < 180**。🔴 **不要靠「看图说看着没问题」**——
当前会话常常读不了图片文件（`Read` 返回不支持图像），那类结论没有证据。

正确的数值法（临时脚本 `/tmp/analyze_circle.py`）：

```python
# 逐像素亮阈值化，量到圆心的最大距离 + 圆外亮像素数
radius = max(sqrt((x-180)**2 + (y-180)**2) for 亮像素)
assert radius <= 180 and 圆外亮像素 == 0
```

实测 launcher 网络页 + 圆屏键盘页：**最大半径 158.9 px，圆外亮像素 0 个** ✓。

想看具体字符（例如键盘输入框里是不是真的打出 `22`），裁一小块出来转 ASCII：

```python
crop = im.crop((86, 68, 150, 95))          # 输入框区域
print('\n'.join('#' if px[x,y] > 140 else ' ' for y in ... for x in ...))
```

⚠️ 顺带一提：合成点按**别用 `'1'`**——它的坐标 (180,180) 正好落在键与键之间那几 px 的缝里，
永远打空且**截图毫无变化**；换一个落在键中心的坐标（如 `'A'` = (84,118)，数字行 `2` 键）。
判定同样以**日志**为准（`cc: kb: tap row=0 col=1 key='2'`），不以截图为准。


## 与「平台层验收」的配合（2026-09-19 补）

截屏只能证明**画面**，证明不了「状态有没有跨 app 传过去」——那类断言要靠**日志链**
（例：启动器 `cc: save: vol=.. bri=..` ↔ 进 app 后 `cc: restore: vol=.. bri=..`）。
现成的两个脚本在 skill `sdgoods-platform-sync`：`verify_settings_sync.py`（音量/亮度跨 app 同步
+ 电池读数，14 判据）、`verify_app0_slot.py`（app0 幂等性 + 电量，8 判据）。

常用串口调试键（**启动器与 app 不是同一套，别混**）：
- 启动器：`'v'` 音量滑块页 / `'w'` 亮度滑块页 / `'c'/'d'/'b'` CC 一级/数据/电量页 /
  `'l'` 启动 slot0 / `'P'` 长按第 2 个图标（+`'o'` Open ⇒ 启动 slot 1，**唯一**能启动非 slot0 的路径）/
  **v1.0.51 新增：`'n'` = 控制中心「网络」页（Wi-Fi 发现）、`'k'` = 网络页再叠圆屏虚拟键盘**
  ⚠️ 网络页**进页即自动扫描**，扫描任务要 1~2s 才回 ⇒ 进 `'n'` 后**至少等 3s** 再 `'s'`，
  否则截到一张空列表（日志对照：`sdgoods_wifi: scan: N ap(s)` + `cc: wifi page: N network(s) listed`）。
- app（BSP 弱默认）：`'C'/'D'/'B'` = CC 一级/数据/电量页
- **截「控制中心 → 关于」页**（发布截图末张必用，has_cc=true 的 app 都要这张）：
  CC 一级页的 About 图标按钮**固定在屏幕 (180,169)**（`sdgoods_cc.c` 里 `make_round_btn(s_cc, 180, 169, 68, CC_ICON_INFO, "About", ...)`）。
  2026-09-27 起一级页改成「按钮+caption 整块垂直居中」，cy 由 **218 上移到 169**（同一行还有 Settings(84,169) / Power(276,169)），
  ⚠️ **旧坐标 (180,218) 已失效** —— 那里落在按钮下缘之外，`'0'` 打空、截图原地不变（2026-09-27 踩过）。
  ⇒ 序列 = `'C'`（开 CC）→ 等 2s → `'1'`（点屏幕中央 (180,180)，正落在 About 圆心 11px 内）→ 等 2.5s → `'s'` 截图。
  三步必须**同一串口会话**连发（`--pre` 只能发一个字符 ⇒ 用多步脚本，见下节）。
  实测 ALLINONE v1.0.0 与 CALC v1.0.0 通过：关于页显示 project_name / v版本号 / 构建时间 / 体积。
  校验硬证据：日志出现 `cc: tap: About` + `cc: about page: <project> | v<x.y.z> | ...`；**别只靠肉眼**，
  三张图若 sha256 完全相同 ⇒ 说明某一步点空了。
- **`'R'` = `esp_restart()`，任何固件都有**。⚠️ 它**不是**「回启动器」：otadata 指向 `ota_N` 时
  会重启回**同一个 app**。要回启动器必须擦 otadata（见 skill `sdgoods-build-flash` §二）。

⚠️ 串口一开就可能有**幻触**（会自己拉出 CC / 滑动图标行 / 甚至点进某个 app）。
所以判据一律建在**日志**上，别建在「屏幕上应该是什么」；并且每轮起手先自证初始状态。

## 「app 烧了却跑的是启动器」：先查 otadata（2026-09-27 踩过）

现象：截图怎么截都同一屏；串口日志出现 `launcher_ui: synth ...`，或
`device_mode: running 'launcher' (subtype=0x00, factory), app='SDGOODS_LAUNCHER' -> MULTI`。
**不要**去查截屏链路。`write_flash 0x10000 <app>.bin` 只写 factory 槽，**改不了启动选择**：
只要 otadata 还指向 `ota_N`（里面存着旧启动器），复位后启动的仍是那个 ota 副本。

一步步核对 / 处置：

```bash
# ① 启动器到底在不在 factory？读 esp_app_desc（magic 0xABCD5432，project_name 在 +0x30）
esptool --chip esp32s3 -p <port> --after no_reset read_flash 0x10020 96 /tmp/fd.bin
# 解析：magic 对上 ⇒ project_name 就是当前 factory 身份（打印错则字节序/偏移不对）

# ② otadata 在 0x310000（本平台分区表，8KB）。全 0xFF ⇒ 回落 factory，即单应用直启
esptool --chip esp32s3 -p <port> erase_region 0x310000 0x2000      # 擦成全 0xFF
# 若 ① 显示 factory 里不是目标 app ⇒ 先 write_flash 0x10000 <app>.bin，再擦 otadata
```
擦完复位，日志应变成 `app='<你的工程名>' (not the launcher) -> SINGLE`。
启动选择判据（`device_mode`）比截图更可靠 —— 截到什么画面可以骗人，这条日志不会。

## 多步串口序列 + 应用上下文 CC 验证（2026-09-22 补）

`--pre` 只能发**一个**字符。要发多步（如「启动某 app → 开 CC → 截图 → 点某键 → 再截图」），
自己写脚本**开一次串口跑完整序列**（反复跑 `screenshot_recv.py` 每次都重开串口 ⇒ 幻触风险翻倍），
直接 `import screenshot_recv as sr` 复用 `sr.open_port()` / `sr.ShotAssembler()`。
范例 `/tmp/verify_bird_cc.py`：`'R'`（先重启，**顺带清掉开串口的幻触状态**）→ `'0'`（点 BTN5=小鸟）
→ `'C'`（开 CC，吃当前 app 上下文）→ `'s'` 截图 → `'h'`（点 CC Home 键）→ `'s'` 再截图。

关键事实：
- **`'C'` 打开的 CC 带当前 app 的上下文**（`sdgoods_cc_set_app_ctx`，如小鸟=3 键精简版）。
  要验某个 app 的 CC 变体，必须先真把那个 app 跑起来再发 `'C'`。
- **`'0'`（点 180,218）落在主页 BTN5（小鸟，左上角 142,191/尺寸 76）**⇒ 可当「启动小鸟」用。
  主页按钮布局变了这里要跟着改。
- **`'h'`**（BSP 弱默认新增）= 合成点按 CC「Home」键 (276,169)——小鸟上下文的返回主页键；
  标准上下文该位置无按钮，是安全空操作。

## 🔴 幻触会自己换页 ⇒ 截图不能按「发完键就截」的时序信标签（2026-09-27 血泪）

`'R'`（重启清幻触）**并不能保证**之后没有幻触。实测同一轮里出现过两次**真实触摸事件**：
- 重启后 **17.0s**：`cc: swipe-down start_y=37 dy=113` ⇒ 把控制中心拉了起来
  ⇒ 计划里的「首页」截图截到的是 CC；
- 28.7s：`swipe-up start=(177,358) dx=3 dy=-125 -> HOME` ⇒ 把刚打开的「关于」页顶回首页
  （露馅的方式很隐蔽：`about.jpg` 与 `home_swipe.jpg` **字节数一模一样**）。

**处置（两条都要做，缺一条就会写出「假红/假绿」的判据）：**

① **截前强制回首页** —— 先 `Q`（收浮层），再**连按 3 次上滑**（launcher 调试键 `'B'`，
   从 (180,336) 上滑）。之所以连按安全：**CC 的每一页都 `sdgoods_swipe_up_bind(page, sdgoods_cc_close)`，
   而首页没有绑** ⇒ 在 CC 里就退出，已经在首页就是空操作。
   （参考脚本 `/tmp/cc69/grab_home.py`）

② **截后按几何自证，再判读** —— 别信「我发完键了所以这张就是 X 页」。
   例：判「这张是首页」= ROI 内存在「宽 30..36 / 高 14..18 / 顶边 y 33..39」的亮连通域（电池外框）。
   ⚠️ **挑部件不能取「面积最大的连通域」**：12px 数字的 bbox 面积可能**大于**空心电池框的
   2px 描边环（数字 24×9 vs 框描边 ~176px）⇒ 会量到字、把框的判据全带歪。
   一律按**几何**（宽/高/顶边）挑，或先按位置切开。
   拿不到预期页面时的稳妥做法：**连拍 N 张、逐张自证、取第一张合格的**。

## 跑验收/分析脚本用哪个解释器

- **只要 pyserial**（纯收发/解析）：`~/.workbuddy/binaries/python/envs/esptool39/bin/python3`。
- **要 pyserial + numpy + PIL**（量化截图）：用
  **`~/.workbuddy/binaries/python/envs/default/bin/python`** —— 三个库都有；
  `envs/esptool39` **没有 numpy**（2026-09-27 就因为这个白跑一次）。
  系统 `/usr/bin/python3` 两个都没有（MEMORY 里那条「esptool 必须用 IDF python」是**烧录**场景，
  与这里不冲突）。
