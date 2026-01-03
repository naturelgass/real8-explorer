# REAL-8 Explorer for Windows

REAL-8 Explorer for Windows is the desktop host for the Real8 VM + Shell. It provides a native SDL window, menu bar, input, and tooling so you can browse, launch, and debug PICO-8 carts on Windows.

> Not affiliated with Lexaloffle Games or PICO-8.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Build requirements](#build-requirements)
- [Build steps (CMake)](#build-steps-cmake)
- [Usage](#usage)
- [Screenshots](#screenshots)
- [Not yet implemented](#not-yet-implemented)
- [Scope](#scope)

## Overview

- Focused Windows host around the core Real8 VM + Shell.
- Optimized for quick cart testing, modding, and debugging.
- Works with local carts or repository lists.

## Features

### Core experience

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from disk, drag-and-drop supported |
| Library and browsing | Local library, repository URL support, remote games with folders and genre |
| Save states | Save and load VM state |
| Wallpaper | Import custom backgrounds, toggle on or off, scaling, persistence |
| Video | Windowed and resizable, fullscreen toggle, stretch modes |
| Effects | Interpolation toggle, CRT scanline filter |
| Audio | Music and SFX toggles with queued-audio sync |
| Screenshots | Save to `Pictures\Real8 Screenshots` or the project screenshots folder |
| Modding | Boot carts with mods created in the Windows version |

### Windows host UI

| Area | Details |
| --- | --- |
| Menu bar | Native Windows menus: File, Options, Settings, Effects, Extra |
| Shell controls | Browse repo games, repo snapshots, and local snapshots |
| Input | Keyboard and SDL gamepad support, remap UI, per-player configs (up to 8 players), thumbstick support |

### Tooling and debugging

| Tooling | Details |
| --- | --- |
| Debug console | Logging, pause and resume, step, breakpoints, memory tools |
| Live Lua injection | PEEK, POKE, and custom commands with console feedback |
| Real-time modding | Live variable editing, favorites, command prompt |
| Export tools | Export GFX, MAP, and music tracks to a chosen folder |
| Crash handling | Exception logging to `logs.txt` and a Windows dialog |

## Supported cart formats

| Format | Notes |
| --- | --- |
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## Build requirements

| Requirement | Notes |
| --- | --- |
| CMake 3.10+ | Build system for the Windows host |
| C++17 compiler | MSVC or MinGW |
| SDL2 | Windowing, input, audio |
| lodepng | PNG loading |

Dependencies can be provided via vcpkg or your preferred package manager.

## Build steps (CMake)

```sh
cmake -S src/platforms/windows -B build/windows
cmake --build build/windows --config Release
```

Optional vcpkg toolchain example:

```sh
cmake -S src/platforms/windows -B build/windows ^
  -DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
```

The build outputs `Real-8 VM.exe` and copies `SDL2.dll` next to it.

## Usage

### Load a cart

- Use the menu: File -> Open, then select a `.p8` or `.png`
- Or drag and drop a cart onto the window

### Save and load state

- Use the menu or the Shell save and load entries

### Repo browsing

- Set a repository URL in the menu, then browse by folders or genre

## Screenshots

### Windows Shell
![Windows Shell](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsShellMenu.png)

### Windows Debug Console
![Windows Debug Console](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsDebugConsole.png)

## Scope

This README describes the Windows host only. Core VM, Shell, and cross-platform features live elsewhere in the REAL-8 Explorer codebase.
