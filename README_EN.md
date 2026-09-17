[![中文](https://img.shields.io/badge/lang-中文-red)](README.md) [![English](https://img.shields.io/badge/lang-English-blue)](README_EN.md)

# SDGOODS Electric Badge · SDGOODS-ESP32S3

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![Platform: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![Apps: Apache-2.0](https://img.shields.io/badge/apps-Apache--2.0-blue.svg)](LICENSING.md)

> [!IMPORTANT]
> This repo is the base firmware/SDK for the SDGOODS Electric Badge (谷仓电子徽章) device.
> Copyright and related rights belong to **Shenzhen Seed Innovation Network Co., Ltd. (SDGOODS)**.
> The code is free to use under this repo's license, but the project name, product name and SDGOODS marks are NOT covered by the code license (see [TRADEMARK.md](TRADEMARK.md)).
> Site [https://sdgoods.ai](https://sdgoods.ai) · Email `zhangzuoliang321@126.com`.

This is a complete firmware example for an **ESP32-S3 + 360×360 round touchscreen** device: boot animation → home → DEMO & samples.
It also ships a from-scratch UI framework, game logic, and a one-command serial screenshot debug pipeline.
From this code, you can easily do AI-assisted secondary development on the SDGOODS Electric Badge (谷仓电子徽章).

| Home | App launcher |
|---|---|
| ![home](docs/images/screenshot-home.png) | ![home2](docs/images/screenshot-home-2.png) |

---

## Hardware

On-board hardware of the SDGOODS Electric Badge:

| Part | Model / spec |
|---|---|
| SoC | ESP32-S3-R8 (dual-core Xtensa LX7, **8MB Octal PSRAM**) |
| Storage | **32MB Flash** (QSPI) |
| Display | Round **360×360**, **ST77916** driver, QSPI, RGB565 |
| Touch | **CST816** capacitive touch (I2C, swipe gestures) |
| IMU | **QMI8658** 6-axis IMU (3-axis accel + 3-axis gyro, I2C) |
| Audio out | On-board Class-D amp + speaker (I2S) |
| Audio in | Digital mic (I2S) |
| Button | 1 power button (GPIO6) |
| Battery | **500mAh** Li-Po + fuel gauge / PMU (GPIO7) |
| Wireless | 2.4GHz **WiFi** + **Bluetooth 5 (BLE)**, two-player BT co-op |
| Interface | USB (USB-Serial-JTAG: flash / debug / serial screenshot) |

**Look & wear:** Round body, **58mm** diameter, **9mm** thick; back has a **magnet** to stick on metal; also a **lanyard hole** and a **badge pin** for two wear styles (lapel badge / pendant).

All pin and panel constants live only in [components/sdgoods_board/include/board_pins.h](components/sdgoods_board/include/board_pins.h); the app layer must not copy them.

---

## DEMO & Reference Apps

The firmware ships several demos that both show what the badge can do and serve as ready-made reference templates for AI. The list below is **not the full hardware catalog** (see "Hardware" above); it picks representative demos to show how the code uses the hardware, and each row notes what it is good to copy as a template:

| Demo or reference file | Hardware demonstrated | What to copy as a template |
|---|---|---|
| Home & app launcher (`ui_home.c`) | Round 360×360 touchscreen, capacitive swipe gestures, boot animation | Shell & launcher structure |
| Demo page (`ui_demo_page.c`) | Basic UI widgets, bilingual text, combined screen+touch calls | Page layout, widgets, hardware calls |
| Flappy bird (`ui_flappy.c`) | Touch control + timer game loop + audio | Simple game: input + timed refresh + draw |
| Plane shooter (`ui_plane.c` + `plane_net.c`) | Touch/gyro control, plus **WiFi + BLE two-player Bluetooth co-op** | Full game + two-player BT sync |
| Minimal skeleton (`main/apps/app_template.c`) | Minimal runnable app (one-shot `tools/new_app.py`) | **Start new apps here**: edit it into your app |
| Serial screenshot | Grab current screen from a PC (debug pipeline) | — |

> After reading these references + [AGENTS.md](AGENTS.md) + [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), you can quickly build the app you want.

---

## 🚀 Start building in one sentence

Want a new app for the badge? Just hand the requirement to your **AI coding assistant** — it runs `tools/check_env.py` to check the environment first, then develops per this repo's rules.

```
Read the code and docs at https://github.com/SDGOODS/SDGOODS-ESP32S3,
and develop a <your-app> app for the SDGOODS badge.
Use touch controls; UI text in both Chinese and English.
Follow AGENTS.md and docs/ARCHITECTURE.md;
complete a runnable implementation and tests,
and give a report with suggestions.
```

The more specific the requirement, the more likely it is correct on the first try: user flow, what buttons/gestures do, whether to persist across power loss, experience goal, acceptance criteria.
If details are missing, the AI may adopt conservative defaults without changing the product direction, but must list its assumptions in the delivery.

> ★ This README only describes the product and repo. **AI must read `AGENTS.md` before coding** — it holds the build method, the two-layer boundary, the font flow, and several hard rules that break things if ignored.

---

## 🤖 AI Dev Toolkit

Besides [`AGENTS.md`](AGENTS.md) (the "must-read spec for AI"), the repo also ships an **AI Dev Toolkit** [`sdgoods-ai/`](sdgoods-ai/README.md): 6 cross-platform Skills (env check / new-app / build-flash / screenshot / fonts / publish — for WorkBuddy / Claude Code / Cursor) + a domain Agent definition + a **local MCP server** (pure stdlib, closing the "AI dev → one-click upload to Open Platform" loop) + per-platform MCP config samples.

- **Install & go**: `bash sdgoods-ai/install.sh` copies the Skills into WorkBuddy; the AI calls them automatically when developing for this device.
- **MCP first, CLI fallback**: publishing prefers the Open Platform MCP; without MCP it falls back to `tools/sdgoods_publish.py` (email-code login, credentials stay local only).
- **Security**: the Open Platform is not open-source; this repo ships only client config samples and protocol notes — **no server code, no secrets**.

See [`sdgoods-ai/README.md`](sdgoods-ai/README.md).

---

## Publish to the SDGOODS Open Platform

Built firmware can be submitted to the **SDGOODS Open Platform** (a plaza where developers upload and others download/flash). Three ways, see [docs/PUBLISHING.md](docs/PUBLISHING.md):

1. **Web UI**: Upload on the web, fill info, send a screenshot (use "screenshot from device" to grab from real hardware), upload the `.bin`.
2. **CLI or AI assistant**: `python3 tools/sdgoods_publish.py login <email>` once, then `publish` — pure stdlib, AI can call it directly.
3. **AI calls the REST API**: Follow the curl examples in `docs/PUBLISHING.md` (auth → presign upload → create record).

> The web "screenshot from device" first sends `?` to probe capability (`SDGOODS-CAPS:SHOT`); **firmware without screenshot support pops a notice**
> "ask the AI to enable screenshot in the BSP (`CONFIG_SDGOODS_SCREENSHOT`) and reflash", instead of grabbing a wrong-colored image blindly.

---

## Project layout

```
components/sdgoods_board/   # Platform layer (Apache-2.0, commercial OK): drivers/LVGL/touch/audio/shell/fonts
  └── include/board_pins.h  # Pin defs (edit here for a new board)
main/apps/                  # App layer (Apache-2.0, where you hack)
  ├── apps_registry.c       # App registry (launcher + poll list)
  ├── app_template.c        # New-app template
  ├── ui_home.c  ui_app_page.c  ui_about.c
  ├── ui_flappy.c           # Flappy bird
  └── ui_plane.c  plane_net.c  # Plane shooter + BT co-op
tools/                      # Scaffolds & debug scripts (Apache-2.0)
  ├── new_app.py            # One command to scaffold + wire an app
  ├── check_env.py          # Env check (AI runs first)
  ├── gen_fonts.py          # Regenerate Chinese subset fonts
  ├── font_metrics.py       # Offline text-width check
  ├── screenshot_recv.py    # PC side of serial screenshot
  └── sdgoods_publish.py    # CLI publish to Open Platform
docs/                       # BUILD / ARCHITECTURE / ENVIRONMENT / PUBLISHING
AGENTS.md                   # ★ Must-read entry for AI assistants
LICENSING.md  TRADEMARK.md  NOTICE  LICENSE
```

---

## Docs index

| Doc | Contents |
|---|---|
| [AGENTS.md](AGENTS.md) | ★ Build method, two-layer boundary, hard rules, pre-commit checklist |
| [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md) | Env requirements & install |
| [docs/BUILD.md](docs/BUILD.md) | Build / flash / package |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layering, hooks, co-op sync, debug pipeline |
| [docs/PUBLISHING.md](docs/PUBLISHING.md) | Publish firmware (web / CLI / API) |
| [LICENSING.md](LICENSING.md) | License scope / commercial FAQ |
| [TRADEMARK.md](TRADEMARK.md) | SDGOODS marks & trademark rules |
| [NOTICE](NOTICE) | Third-party components & attributions |

---

## License (summary)

This repo (platform + app layers) is **uniformly released under Apache-2.0**: free for commercial and non-commercial, can be closed-source, modified, and redistributed.
The license covers code only, **not trademarks** (see [TRADEMARK.md](TRADEMARK.md)).

| Part | License | Commercial |
|---|---|---|
| `components/sdgoods_board/` (Platform layer) | Apache-2.0 | ✅ Free |
| `main/` (App layer) | Apache-2.0 | ✅ Free |
| `tools/` (Scripts) | Apache-2.0 | ✅ Free |
| `main/patches/` (LVGL patches) | MIT | ✅ Free (LVGL's license) |
| `components/sdgoods_board/fonts/` (Subset fonts) | SIL OFL 1.1 | ✅ Free (font license) |

License scope and trademark use: see [LICENSING.md](LICENSING.md) / [TRADEMARK.md](TRADEMARK.md).

---

## Contact

- **Site**: [https://sdgoods.ai](https://sdgoods.ai) (SDGOODS Open Platform)
- **Email**: `zhangzuoliang321@126.com` — for commercial licensing, bug reports, and partnerships

---

## Acknowledgements

- [LVGL](https://lvgl.io/) · [ESP-IDF](https://github.com/espressif/esp-idf) · [Noto Sans SC](https://github.com/notofonts/noto-cjk) · [gifdec](https://github.com/lecram/gifdec)
