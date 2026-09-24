# 第三方组件补丁：LVGL 8.3.11 的 GIF 解码器走 PSRAM

## 为什么要有这个目录

`managed_components/` 是 ESP-IDF **组件管理器自动下载**的第三方代码，按惯例**不入库**
（本仓库 `.gitignore` 已忽略）。但本项目的开机动画依赖对 LVGL 内置 GIF 解码器的改动 ——
如果这处改动只存在于本地 `managed_components/`，那么别人 clone 本仓库后**能编译、能烧录，
但开机动画会在运行时分配失败**（表现是开机动画黑屏 / 重启），而且很难查。

所以把改好的文件放在这里随源码提交，并由 `main/CMakeLists.txt` 在每次 CMake configure
阶段调用 `apply_lvgl_patches.py` 覆盖到 `managed_components/` 下，做到「clone 即可复现」。

## 改了什么

对 `lvgl__lvgl/src/extra/libs/gif/` 下的 **3 个文件**（`gifdec.c`、`gifdec.h`、`lv_gif.c`）：

| # | 文件 | 改动 | 原因 |
|---|---|---|---|
| 1 | gifdec.c | `canvas` / `frame` 用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 分配 | 360×360 的 canvas 在 16bpp 下约 518KB（240×240 也有 ~115KB），远超 LVGL 内部内存池；不放 PSRAM 会分配失败 |
| 2 | gifdec.c | canvas 由 RGB888（4B/px）改为 **RGB565**（2B/px） | PSRAM 写带宽是这块芯片解码速度的瓶颈，减半后单帧显著变快 |
| 3 | gifdec.c | `gd_open_gif_data()` 把 GIF 源数据从 flash 拷到 RAM（优先内部 SRAM，退化 PSRAM） | LZW 逐字节读 flash 会与取指争同一条总线，实测解码慢到 ~85ms/帧，撑不住 15fps |
| 4 | gifdec.h | `gd_GIF` 增加 `data_owned` 字段，`gd_close_gif()` 里释放拷贝 | 配套 #3 的内存释放，避免泄漏 |
| 5 | gifdec.h | `gd_open_gif_data()` 增加 `data_size` 参数 | 配套 #3：拷贝源数据必须知道长度 |
| 6 | lv_gif.c | 调用处传 `img_dsc->data_size` | 配套 #5 |
| 7 | lv_gif.c | `imgdsc.header.cf`: `LV_IMG_CF_TRUE_COLOR_ALPHA` → `LV_IMG_CF_TRUE_COLOR` | 配套 #2：RGB565 canvas 没有 alpha 通道，否则颜色被当成带 alpha 解析、整屏发花 |

> ⚠️ **这 3 个文件是一套的，必须整组替换。** 我们踩过：只把 gifdec 两个文件补上、漏了
> `lv_gif.c`，结果是 `too few arguments to function 'gd_open_gif_data'` 编译失败 ——
> 也就是说，漏掉任何一个，开源仓库里「clone 后直接编译」都会失败或画面异常。
> `apply_lvgl_patches.py` 现在会对这 3 个文件做**逐字节**比对校验，不做标记串匹配。


## 许可：本目录沿用 MIT，**不受**本仓库 Apache-2.0 许可约束

上游 LVGL 及其 `gifdec` 均为 **MIT** 许可，允许修改与再分发。

本目录下的文件是**对 LVGL 源代码的衍生作品**，因此它们**继续以 MIT 分发**。
即使本仓库根部以 Apache-2.0 统一发布，**这个目录仍按 MIT 处理**（我们无权对别人的
MIT 代码追加限制）。

换句话说：这里的东西你可以自由商用、自由再分发，与 LVGL 本体待遇相同。

理由有两条：

1. 我们无权对别人的 MIT 代码追加限制；
2. 硬要在一个文件里塞两套互相冲突的许可，只会让认真读许可的人不再信任这份仓库。

本目录文件顶部保留了上游来源与改动说明，请一并保留。

## 怎么工作

```
main/CMakeLists.txt
  └─ configure 阶段 execute_process → main/patches/apply_lvgl_patches.py
        └─ 对本目录 3 个文件与目标文件做**逐字节比对**（filecmp shallow=False）
              · 相同  → 跳过（幂等）
              · 不同  → 覆盖，写完再逐字节复查一次
              · 目标不存在（组件还没下载下来）→ 只警告，返回 0，下次 configure 补上
              · 写完仍不一致 → 返回非 0，configure 直接 FATAL_ERROR
```

> ⚠️ **判断「是否已打补丁」必须逐字节比较，不能用「标记串是否出现」。**
> 上游文件里可能恰好包含该字符串（我们最初的 `MALLOC_CAP_SPIRAM` 标记就在
> `gifdec.h` 上误命中过一次），串匹配会误判成「已是最新」而跳过整个文件，
> 最终表现为编译错误且很难定位。

脚本幂等，可以反复 configure；如果「该打补丁却打不上」，脚本以非零码退出，**构建会直接失败**
（宁可在编译期报错，也不要产出一个开机动画崩掉的固件）。

## 升级 LVGL 时怎么办

1. 改 `main/idf_component.yml` 里的 `lvgl/lvgl` 版本，重新 `idf.py build` 让管理器下载新版；
2. 对比新版上游 `gifdec.c` 与本目录的文件（上游可能已改过同一处），手工合并改动；
3. 把合并结果更新回本目录，改目录名里的版本号，并同步更新 `apply_lvgl_patches.py`
   的 `PATCHES` 表与上面的说明；
4. 必须实测一遍开机动画（帧率、颜色、无花屏），这条链路不跑一遍不能算完成。
