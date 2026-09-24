[![中文](https://img.shields.io/badge/lang-中文-red)](README.md) [![English](https://img.shields.io/badge/lang-English-blue)](README_EN.md)

# Barn Dimension Screen · SDGOODS-ESP32S3

[![Build](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml/badge.svg)](https://github.com/SDGOODS/SDGOODS-ESP32S3/actions/workflows/build.yml)
[![Platform layer: Apache-2.0](https://img.shields.io/badge/platform-Apache--2.0-blue.svg)](LICENSE)
[![App layer: Apache-2.0](https://img.shields.io/badge/apps-Apache--2.0-blue.svg)](LICENSING.md)

> [!IMPORTANT]
> This repository is the base project for second development on the **Barn Dimension Screen** (Barn Electronic Badge) device.
> Copyright and related rights belong to **Shenzhen Xide Innovation Network Co., Ltd. (SDGOODS)**.
> The code is freely usable under this repository's license, but the **project name, product name, and SDGOODS marks are NOT covered by the code license** (see [TRADEMARK.md](TRADEMARK.md)).
> Website [https://sdgoods.ai](https://sdgoods.ai) · Email `zhangzuoliang321@126.com`.

---

## Project Goal

> **Enable anyone — through the AI coding assistant they already use — to go from cloning this repo on GitHub → deriving their own app → developing it → publishing it to the Barn Open Platform, all within 5 minutes, without first becoming an embedded expert.**

This repository is designed to be "**AI-readable, AI-editable, AI-publishable**":

- **AI can read it**: `AGENTS.md` is the must-read spec before an AI touches the code; `docs/` contains complete layered-architecture, build, publishing, and SDK documentation; `sdgoods-ai/` is a one-command-installable AI development toolkit (Skills + Agent + local MCP server).
- **AI can edit it**: the repo ships a hand-written UI framework and several samples (Flappy game / home launcher / scan & recognize demo pages) — both a showcase of what the board can do and ready-made templates for AI to modify.
- **AI can ship it**: the bundled publishing pipeline closes the loop "develop → package → upload to the Barn Open Platform" into one command / one MCP call.

Just hand your requirement to your AI assistant in plain language; it will build the app following this repo's specs and help you publish it.

---

## Barn Electronic Badge (Hardware)

An **ESP32-S3 + 360×360 round touch screen** wearable badge that can be worn as a name tag / pendant. On-board hardware:

| Part | Model / Spec |
|---|---|
| SoC | ESP32-S3-R8 (dual-core Xtensa LX7, **8MB Octal PSRAM** on-chip) |
| Storage | **32MB Flash** (QSPI) |
| Display | Round **360×360**, **ST77916** driver, QSPI, RGB565 |
| Touch | **CST816** capacitive touch (I2C, gesture support) |
| Motion | **QMI8658** 6-axis IMU (3-axis accelerometer + 3-axis gyroscope, I2C) |
| Audio out | Class-D amplifier + speaker (I2S) |
| Audio in | Digital microphone (I2S) |
| Physical key | 1 power key (GPIO6) |
| Battery | **500mAh** Li-ion + battery gauge / power management (GPIO7) |
| Wireless | 2.4GHz **WiFi** + **Bluetooth 5 (BLE)**, supports 2-player Bluetooth battles |
| Interface | USB (USB-Serial-JTAG, for flashing / debug / serial screenshots) |

**Appearance & wearing**: round body, **58mm** diameter, **9mm** thick; **magnet** on the back for metal surfaces; plus a **lanyard hole** and a **badge pin**.

> Why these specs matter: the 360×360 **circle** clips the corners — all UI must fit inside the round-screen safe rectangle (the platform provides `SDG_UI_SAFE_X/Y/W/H` grid constants); 8MB PSRAM fits full LVGL frame buffers and fairly large image assets. The app layer must **not** copy pin constants from `components/sdgoods_board/include/board_pins.h`.

### On-device preview (samples shipped in this repo: Home + Flappy bird, single-player)

| Home | Flappy bird |
|---|---|
| ![Home](screenshot/home.jpg) | ![Flappy bird](screenshot/game-flappy.jpg) |

---

## What Is the Barn Open Platform?

The [Barn Open Platform](https://sdgoods.ai) is the **app market / plaza** for this badge:

- **For developers**: upload your compiled app firmware (a single `.bin`), fill in name / description / category / screenshots, submit for **review**, and once approved it goes **live** — other users can find it and install it on their badges.
- **For end users** (badge owners): browse apps, one-click download, and flash to their badge via Web Serial or platform tools; one badge supports **multiple app slots** with free switching.
- **For AI / CI**: the platform provides an **MCP developer interface** (just a `sdg_` developer token), so your AI assistant can push finished firmware straight to the platform — no manual form filling.

Key rules every developer must know:

- **You only upload a "pure app image"**: the platform automatically prepends the official bootloader (bootloader / partition table), so your `.bin` carries **no flash addresses** and no factory partitions.
- **Review**: submissions enter a review queue and go public once approved; any app should only ever have **one** record on the platform (re-publishing = delete the old record, then create the new one).
- **Multi-app vs single-app**: the same `app.bin` can be listed as one app of the "multi-app mode" launcher, or flashed as a "single-app mode" standalone firmware — determined at runtime by the device, no dual builds needed (see `docs/SINGLE_APP_FIRMWARE.md`).

---

## 🚀 5 Minutes: Generate Your First App

You do **not** need to know C or ESP-IDF first. Three steps, then hand the requirement to AI:

```bash
# 1) Clone this repo
git clone https://github.com/SDGOODS/SDGOODS-ESP32S3
cd SDGOODS-ESP32S3

# 2) (Recommended) Install the AI toolkit: WorkBuddy / Claude Code / Cursor
#    will automatically apply this repo's specs when working on this device
bash sdgoods-ai/install.sh

# 3) Derive your own standalone app project in one command
#    (independent name, boots straight into your app)
python3 tools/new_app_project.py MyApp
```

The derived `MyApp/` is a **fully buildable, standalone project that boots straight into your app**: no home screen, no launcher, no demos — power on and you are in your UI.

Then hand the requirement to your AI assistant (Chinese or English):

```
Read the code and docs at https://github.com/SDGOODS/SDGOODS-ESP32S3,
and build <your app> for the Barn Electronic Badge based on the MyApp project.
Requirements: <describe in plain words what it should do, what taps/gestures do, whether data is saved>.
Use bilingual (Chinese/English) UI strings. Follow AGENTS.md and docs/ specs; deliver a runnable implementation.
```

The more specific the requirement (user flow, key/gesture behavior, persistence, acceptance criteria), the more likely it is right the first time. If details are missing, the AI will use conservative defaults and list its assumptions.

> ★ This README covers product & workflow only. **An AI must read `AGENTS.md` before touching the code** — that's where the build commands, the two-layer boundary, the font pipeline, and several "break-the-bug" hard constraints live.

---

## Your First App Can Already Do All This

The minimal app generated by `new_app_project.py` comes from `app_template.c` in this repo — **not an empty shell**, but already wired to every standard platform capability:

| Capability | What it means | Where you change it |
|---|---|---|
| **Boots straight into your app** | No home / launcher / demos; power on → your UI | `ui_<name>.c` |
| **Round-screen safe area** | Built-in `SDG_UI_SAFE_X/Y/W/H` grid; widgets never get clipped by the round edge | Reference the constants in layout |
| **Touch interaction** | Tap buttons, tap anywhere, swipe-left-to-go-back — all via proven platform primitives (no hand-written gesture detection) | `sdgoods_app_on_tap` / `sdgoods_app_on_gesture` |
| **Top pull-down menu** | Pull down from the top for the control center: volume ± / brightness / screenshot / exit | Provided automatically by the platform layer |
| **Control center / About page** | Volume, brightness, device info (incl. firmware version) — zero code from you | Provided automatically by the platform layer |
| **Platform sound effects** | Call `sdgoods_audio_sfx_flap()` to play a cue sound, safe with no side effects | In your event callbacks |
| **Bilingual strings** | Write `SDG_T("中文", "English")`; users can switch language in settings | All UI strings |
| **One-key serial screenshot** | Connect to a computer and capture the current screen (for debugging and store screenshots) | `tools/screenshot_recv.py` |
| **Per-frame loop** | `ui_<name>_poll()` runs every frame for animation / game logic | Your `poll` function |
| **Lifecycle** | pause (frozen when the menu overlays) / resume / exit (cleanup) callbacks | The corresponding callbacks |
| **Persistent storage** | Store data via the `appdata` partition (already reserved) | `APP_SDK.md` |

> ⚠️ **The one rule you must never forget**: any **new Chinese text** in the UI requires re-running the font subset tool, otherwise those characters render as boxes (tofu):
> ```bash
> python3 tools/fetch_fonts.py   # first time only: download the OFL source fonts
> python3 tools/gen_fonts.py
> ```

---

## Publish Your First App to the Platform

Once development is done and verified on hardware, there are two official publishing paths (recommended order: MCP → web):

### Path 1 · MCP developer token (recommended, for AI / CI)

1. Generate a `sdg_` developer token in the developer settings of the Barn Open Platform.
2. Package the pure app image (the platform auto-prepends the bootloader):
   ```bash
   idf.py -B build build
   python3 tools/pack_app.py -b build      # produces dist/<name>_app.bin
   ```
3. Save the developer token (once), then push with the token (your AI can also call the MCP server directly):
   ```bash
   # Save the token (or temporarily: export SDGOODS_DEV_TOKEN="sdg_your_token")
   python3 tools/sdgoods_publish.py set-token "sdg_your_token"

   python3 tools/sdgoods_publish.py mcp-upload \
     --file dist/<name>_app.bin \
     --name "MyApp" --category tool \
     --desc-zh "..." --desc-en "..." \
     --shots shot1.jpg shot2.jpg shot3.jpg shot4.jpg
   ```
   The submission enters review and goes live once approved. When re-publishing, run `mcp-replace <old_id>` first to delete the old record, then upload.

### Path 2 · Web manual (simplest human path)

Open [sdgoods.ai](https://sdgoods.ai) → upload `dist/<name>_app.bin` → fill in name / description / category → upload screenshots (or click "Capture from device" to grab a real-device shot) → submit for review.

### Other CLI subcommands (token channel)

`tools/sdgoods_publish.py` also provides query & maintenance subcommands: `mcp-firmwares` (list your own submissions),
`mcp-unpublish` (delist), `mcp-delete` (delete draft/rejected records),
`mcp-download <id> --check-sha256 <local sha256>` (fetch the platform's flash manifest for an anti-brick check).
See `python3 tools/sdgoods_publish.py --help` and [docs/PUBLISHING.md](docs/PUBLISHING.md).

> The web's "Capture from device" first sends `?` to probe firmware capabilities (`SDGOODS-CAPS:SHOT`); **firmware without screenshot support gets a prompt to enable `CONFIG_SDGOODS_SCREENSHOT` in the BSP and reflash**, instead of blindly grabbing a color-scrambled image.

---

## 🤖 How AI Should Read This Repo

Besides `AGENTS.md` (the must-read spec), the repo ships an **AI development toolkit** [`sdgoods-ai/`](sdgoods-ai/README.md): 6 cross-platform Skills (env check / new app / build & flash / screenshot / fonts / publishing, supporting WorkBuddy / Claude Code / Cursor) + one domain Agent definition + a **local MCP server implementation** (pure standard library; closes the loop "AI develops → one-click upload to the open platform") + MCP config samples for each platform.

- **Install and go**: `bash sdgoods-ai/install.sh` installs the Skills into WorkBuddy; the AI invokes them automatically whenever this device is involved.
- **Safe by design**: the open platform itself is not open source; this repo contains only client-side config samples and protocol docs — **no server code and no secrets**.

---

## Project Structure

```
components/sdgoods_board/   # Platform layer (Apache-2.0, commercial use OK): drivers/LVGL/touch/audio/shell/fonts
  └── include/board_pins.h  # Pin definitions (change here for a different board)
main/apps/                  # App layer (Apache-2.0 — where most second development happens)
  ├── apps_registry.c       # App registry (what the home screen shows, who gets polled)
  ├── app_template.c        # New-app template (copied by new_app_project.py)
  ├── ui_home.c             # Home screen (single-app first screen)
  ├── ui_scan_page.c  ui_rec_page.c  ui_other_page.c   # Sample pages
  └── ui_flappy.c           # Flappy bird (game demo)
tools/                      # Scaffolding & debug scripts (Apache-2.0)
  ├── new_app_project.py    # Derive a standalone app project from scratch (boots straight in, listed independently)
  ├── check_env.py          # Dev environment check (always the AI's first step)
  ├── gen_fonts.py          # Regenerate the Chinese subset fonts
  ├── screenshot_recv.py    # PC-side serial screenshot receiver
  ├── pack_app.py           # Package a pure app image (for publishing)
  └── sdgoods_publish.py    # CLI / MCP firmware submission to the open platform
platform/                   # ★ Platform-managed layer: do not modify, do not commit changes
  ├── partitions.csv        # Multi-app partition table (overwritten by the official one at install time)
  └── prebuilt/             # Prebuilt bootloader for local debugging
docs/                       # BUILD / ARCHITECTURE / ENVIRONMENT / PUBLISHING / APP_SDK ...
AGENTS.md                   # ★ Must-read entry for AI coding assistants
LICENSING.md  TRADEMARK.md  NOTICE  LICENSE
```

---

## Documentation Index

| Doc | Content |
|---|---|
| [`AGENTS.md`](AGENTS.md) | ★ Build commands, two-layer boundary, hard constraints, pre-commit checklist for AI |
| [`docs/ENVIRONMENT.md`](docs/ENVIRONMENT.md) | Dev environment requirements & setup (Python / ESP-IDF / esptool) |
| [`docs/BUILD.md`](docs/BUILD.md) | Build / flash / package & distribute |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Layered design, hooks, multiplayer sync, debug pipeline |
| [`docs/APP_SDK.md`](docs/APP_SDK.md) | App-side SDK and multi/single-app mode detection |
| [`docs/SINGLE_APP_FIRMWARE.md`](docs/SINGLE_APP_FIRMWARE.md) | Run your app as the host firmware (single-app mode) |
| [`docs/PUBLISHING.md`](docs/PUBLISHING.md) | Submitting firmware to the open platform (web / CLI / MCP) |
| [`docs/MCP_CONTRACT.md`](docs/MCP_CONTRACT.md) | MCP / publishing field contract (local stdio server vs platform-hosted MCP) |
| [`LICENSING.md`](LICENSING.md) | Licensing scope / commercial licensing / FAQ |
| [`TRADEMARK.md`](TRADEMARK.md) | SDGOODS marks & trademark rules |
| [`NOTICE`](NOTICE) | Third-party components & attribution |

---

## License (Summary)

This project (platform layer and app layer) is released under **Apache-2.0**: free for commercial and non-commercial use, closed-source derivatives allowed, modification and redistribution allowed. The license covers the code only — **not the trademarks** (see [TRADEMARK.md](TRADEMARK.md)).

| Part | License | Commercial use |
|---|---|---|
| `components/sdgoods_board/` (platform layer) | Apache-2.0 | ✅ Free |
| `main/` (app layer) | Apache-2.0 | ✅ Free |
| `tools/` (scripts) | Apache-2.0 | ✅ Free |
| `main/patches/` (LVGL patches) | MIT | ✅ Free (upstream LVGL license) |
| `components/sdgoods_board/fonts/` (subset fonts) | SIL OFL 1.1 | ✅ Free (upstream font license) |

---

## Contact

- **Website / Open Platform**: [https://sdgoods.ai](https://sdgoods.ai)
- **Email**: `zhangzuoliang321@126.com` — commercial licensing, bug reports, partnerships

---

## Acknowledgements

- [LVGL](https://lvgl.io/) · [ESP-IDF](https://github.com/espressif/esp-idf) · [Noto Sans SC](https://github.com/notofonts/noto-cjk) · [Source Han Sans](https://github.com/adobe-fonts/source-han-sans) · [gifdec](https://github.com/lecram/gifdec)
