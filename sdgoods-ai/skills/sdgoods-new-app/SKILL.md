---
name: sdgoods-new-app
description: 在谷仓次元屏（SDGOODS-ESP32S3）上新增一个应用骨架：封装 tools/new_app.py，一键生成 main/apps/<app>.c 并自动注册到启动台与每帧轮询（apps_registry.c）。封装两层架构接线点、保留 new_app.py 的插入标记。Use when asked to add / 新建 / scaffold a new app for the badge.
agent_created: true
---

# 新增应用骨架

## 何时使用
- 用户要「做个新应用 / 加个 demo / 写个自己的程序」。

## 运行（从仓库根目录）
```bash
python3 tools/new_app.py <app_id> "<显示名>"
# 例：python3 tools/new_app.py my_app "我的应用"
```
自动完成：
1. 生成 `main/apps/<app_id>.c`（最小可运行骨架，含 `SDG_T` 双语、触摸、绘制样例）。
2. 修改 `main/apps/CMakeLists.txt` 加入源文件。
3. 在 `main/apps/apps_registry.c` 的 `s_apps[]` 注册（驱动启动台按钮 + 每帧轮询）。

## 接线点（应用层唯一入口）
- `apps_registry.c` 的 `s_apps[] = {label, show, poll}` 是唯一接线点，同时驱动启动台与轮询。
- 平台层**不得 include 应用层头文件**；应用回调平台用 `sdgoods_hooks.{h,c}` 函数指针表
  （未注册即空操作）：`sdgoods_apps_set_poll()` + `sdgoods_ui_set_nav()`。装配点唯一：
  `main.c` 的 `apps_register()`，**必须排在 `ui_boot_show()` 之前**。

## 硬约束（来自 AGENTS.md / ARCHITECTURE.md）
- ⚠️ **不要删 `apps_registry.c` / `CMakeLists.txt` 里的 `# >>> new_app.py: ... >>>` 标记**——
  `new_app.py` 靠它在文件里定位插入点；删了下次加应用会失败或重复插入。
- 新应用只放 `main/apps/`；硬件逻辑留在 `components/sdgoods_board/`（平台层）。
  应用**不得复制**引脚/面板常量（只在 `components/sdgoods_board/include/board_pins.h` 定义）。
- 双语文案一律 `SDG_T("中文","English")`。
- 源文件头需带 Apache-2.0 声明；新文件跑 `python3 tools/add_license_headers.py --apply` 盖章。

## 验证
- `python3 tools/check_env.py` → 编译前先确认环境。
- 编译/烧录见 skill `sdgoods-build-flash`；改了中文后必须重跑 `sdgoods-fonts`。
