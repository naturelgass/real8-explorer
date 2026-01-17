# REAL-8 Explorer

A PICO-8 explorer + emulator front-end for multiple platforms.

**REAL-8 Explorer** is the “comfort layer” around the **Real8 VM + Shell** — a native host UI for windowing, menus, input, and tooling — so you can **browse, launch, and debug carts** with less friction.

> Not affiliated with Lexaloffle Games, PICO-8, Nintendo, or RetroArch/Libretro.

---

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/naturelgass/real8-explorer)

## Table of contents

- [Platforms](#platforms)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Usage](#usage)
- [What needs more work](#what-needs-more-work)
- [GBA implementation](#gba-implementation)
- [Why](#why)
- [Screenshots](#screenshots)
- [Contributing](#contributing)
- [License](#license)

---

## Platforms

| Target | Host / Integration | Status | Details |
|------:|---------------------|--------|---------|
| **Windows** | SDL host + native menus/tooling | ✅ | [README](src/platforms/windows/README.md) |
| **Nintendo Switch** | libnx host integration | ✅ | [README](src/platforms/switch/README.md) |
| **Nintendo 3DS** | citro2d / citro3d | ✅ | [README](src/platforms/3ds/README.md) |
| **Nintendo GBA** | devkitARM / GBA Sdk | ✅ | [README](src/platforms/gba/README.md) |
| **Libretro** | Libretro API core | ✅ | [README](src/platforms/libretro/README.md) |

---

## Features

### REAL-8 extensions

| Feature | Description |
|---------|-------------|
| **Framebuffer modes** | Extended game resolutions: (GBA) 240x160 / (3DS) 320x240 / (SWITCH) 640x360 * 2 (Full screen on each platform) |
| **Stereo depth pipeline** | Stereo on 3DS or RED/CYAN on rest platforms - per-sprite depth buckets |
| **Motion sensors** | Accelerometer / Gyroscope readings for 3DS and Switch |

All new features are demonstrated in [PICO EXTENDED](demos/pico_extended.p8)

### Core experience

| Area | Details |
|------|---------|
| **Cart loading** | Load carts from disk (`.p8` / `.png`), drag-and-drop on desktop |
| **Library & browsing** | Repository URL support, Local library or Remote games with folders / Genre |
| **Save states** | Save and load VM state |
| **Wallpaper** | Import custom background / Optionl ON or OFF |
| **Video** | Fullscreen toggle, stretch modes, interpolation toggle, optional CRT scanline filter |
| **Effects** | Interpolation toggle, CRT scanline filter |
| **Audio** | Music + SFX toggles, queued-audio sync |
| **Input** | Keyboard + controller support, HID (Thumbsticks support) remapping UI, per-player configs (up to 8 players) |
| **Modding** | You can boot games with Mods created through the Windows version |

### Developer / power-user tooling

| Tooling | Details |
|--------|---------|
| **Debug console** | Logging, pause/resume, step, breakpoints, memory tools |
| **Real-time modding** | Live variable editing, favorites, command prompt |
| **Live LUA injection** | PEEK, POKE or anything else with feedback on the Console |
| **Export tools** | Export **GFX**, **MAP**, and **music tracks** to a chosen folder |
| **Crash handling (Windows)** | Exception logging to `logs.txt` + a Windows dialog |

---

## Supported cart formats

| Format | Notes |
|-------:|------|
| `.p8` | Text cart source |
| `.png` | Image/cart container |

---

## Usage

### Load a cart (Windows)

- **File → Open** and select a cart
- **or** Drag a `.p8` / `.png` file onto the window

### Load a cart (Switch)

- **Local Games** place your games in the Switch/Real8/ folder
  **or**
- **Local Repo JSON** place your [gameslist.json](https://github.com/naturelgass/real8games/blob/main/gameslist.json) in the Switch/Real8/Config folder
  **or**
- **Remote Games** point to remote JSON file in the gamesrepo.txt
 
---

### Save / load state

- On Windows, Menu to Load/Save, or the Shell Menu.
- On Switch, use the Shell Menu
- On RetroArch, use default RetroArch save system

---

### Games Compatibility

- Currently supports about 80-90% of PICO-8 games
- On Switch is compiled to work with FW 21.1.0 | AMS 1.10.1
- On the 3DS it is optimized, but in some games it runs slower

---

### What needs more work

- The Sound Engine in some games might not emulate correctly
- Save state (only works in a few games, in others it loads the game corrupted)
- Compatibility is good, but I would like to support the difficult ones as well (Poom, R-Type etc)

---

## GBA implementation

The GBA target packages a single `.p8.png` cart into a standalone GBA ROM and boots it directly (no shell).
It renders a 128x128 framebuffer centered in Mode 4 and leans on IWRAM/EWRAM optimizations to hit speed.
### This implementation is optimized to run on a Real GBA hardware! ###
See [GBA README](src/platforms/gba/README.md) for full build steps, memory layout, and tuning tips.

---

### Why

- I always wanted to build an emulator (PICO-8 is a good start)
- The unofficial PICO-8 emulation was very cumbersome to use
- Learning experience with homebrew libraries

---

## Screenshots

### Nintendo Switch
![REAL-8 on Switch](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8Switch.png)

### Nintendo 3DS
![REAL-8 on 3DS](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-3DS.png)

### Nintendo GBA
![REAL-8 on GBA](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-Pico2GBA-Rom.png)

### Windows – Debug Console
![Windows Debug Console](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsDebugConsole.png)

### Windows – Shell
![Windows Shell](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsShellMenu.png)

---

### Windows (SDL)
- Toolchain: MSVC or MinGW
- Dependencies: SDL2

### Nintendo Switch (libnx)
- Toolchain: devkitPro + libnx 4.10.0
- Works with FW 21.1.0 | AMS 1.10.1

### Nintendo 3DS (citro2d/citro3d)
- devkitPro + 3ds-dev + 3dstools
- 3dstool (for packing assets)
- Curl (for online feching)

### Nintendo GBA (devkitARM / GBA Sdk)
- devkitPro / devkitARM + GBA Sdk
- Pico2GBA GUI Packer/Converter

### Libretro
- Libretro toolchain / RetroArch environment

---

## Contributing

When reporting bugs, please include:
- Target (**Windows / Switch / Libretro**)
- Cart format (`.p8` / `.png`) and a repro case
- Logs (e.g., `logs.txt` on Windows) if available

---

## License

This project is licensed under **GPL-3.0** (see `LICENSE`).

---

***REAL-8” started as a backronym (“Real-Time Emulation Analog Layer”), but it stuck***

