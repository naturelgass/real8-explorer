# REAL-8 Explorer

A **PICO-8–style explorer & emulator** front-end for multiple platforms.  
REAL-8 Explorer wraps the **Real8 VM + Shell** with a native host UI (windowing, menus, input, tooling) so you can **browse, load, and run carts** comfortably on desktop and handheld targets.

> **Not affiliated with** Lexaloffle Games, PICO-8, Nintendo, or RetroArch/Libretro.

---

## Table of contents

- [Platforms](#platforms)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Usage](#usage)
- [Screenshots](#screenshots)
- [Building](#building)
- [Contributing](#contributing)
- [License](#license)

---

## Platforms

| Target | Host / Integration | Status |
|------:|---------------------|--------|
| **Windows** | SDL host + native menus/tooling | ✅ |
| **Nintendo Switch** | libnx host integration | ✅ |
| **Libretro** | Libretro API core | ✅ |

---

## Features

### Core experience

| Area | Details |
|------|---------|
| **Cart loading** | Load carts from disk (`.p8` / `.png`), drag-and-drop on desktop |
| **Library & browsing** | Repository URL support, browse repo games, repo snapshots, and local snapshots |
| **Save states** | Save and load VM state |
| **Screenshots** | Capture screenshots to `Pictures\Real8 Screenshots` (or project screenshots folder) |
| **Wallpaper** | Import wallpaper, scale, persist to disk |
| **Video** | Fullscreen toggle, stretch modes, interpolation toggle, optional CRT scanline filter |
| **Audio** | Music + SFX toggles, queued-audio sync |
| **Input** | Keyboard + controller support, remapping UI, per-player configs |

### Developer / power-user tooling

| Tooling | Details |
|--------|---------|
| **Debug console** | Logging, pause/resume, step, breakpoints, memory tools |
| **Real-time modding** | Live variable editing, favorites, command prompt |
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
  **or**
- Drag a `.p8` / `.png` file onto the window

### Browse a repository

- Set the **Repository URL**
- Use the browser controls to view:
  - Repo games
  - Repo snapshots
  - Local snapshots

### Save / load state

Use the corresponding actions from the menus (or your configured hotkeys, if available).

---

## Screenshots

### Windows – Shell
![Windows Shell](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsShell.png)

### Nintendo Switch
![REAL-8 on Switch](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8Switch.png)

### Windows – Debug Console
![Windows Debug Console](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsDebugConsole.png)

---

## Building

This repository currently focuses on the **Explorer/host layer**. Depending on your setup, you may also need the Real8 VM/Shell components.

If you maintain build scripts, place quick commands here (CMake, make, devkitPro, etc.). Example layout:

### Windows (SDL)
- Toolchain: MSVC or MinGW
- Dependencies: SDL2

### Nintendo Switch (libnx)
- Toolchain: devkitPro + libnx

### Libretro
- Build via your Libretro toolchain / RetroArch environment

> If you want, I can tailor this section to your exact build commands once you share the current build instructions (CMakeLists, Makefile targets, etc.).

---

## Contributing

Issues and PRs are welcome.

When reporting bugs, please include:
- Target (**Windows / Switch / Libretro**)
- Cart format (`.p8` / `.png`) and a repro case
- Logs (e.g., `logs.txt` on Windows) if available

---

## License

This project is licensed under **GPL-3.0** (see `LICENSE`).
