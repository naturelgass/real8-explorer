# REAL-8 Explorer for Linux

REAL-8 Explorer for Linux is the SDL2 host for the Real8 VM + Shell. It provides a resizable desktop front-end for browsing and running PICO-8 carts on Linux.

> Not affiliated with Lexaloffle Games or PICO-8.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Data paths](#data-paths)
- [Build requirements](#build-requirements)
- [Toolchain setup (Linux)](#toolchain-setup-linux)
- [Build steps (CMake)](#build-steps-cmake)
- [Usage](#usage)
- [Screenshots](#screenshots)
- [Notes and limitations](#notes-and-limitations)

## Overview

- Linux desktop host for the Real8 VM + Shell (SDL2).
- Windowed and resizable with fullscreen toggle.
- Uses XDG config and data directories.

## Features

### Core experience

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from the data directory, drag-and-drop supported |
| Library and browsing | Local library, favorites, repository URL support, remote games with folders and genre |
| Save states | Save and load VM state from the Shell menu |
| Wallpaper | Custom `wallpaper.png` |
| Video | Resizable window, fullscreen toggle, stretch modes, interpolation toggle |
| Effects | Optional CRT scanline filter |
| Audio | SDL queued audio output |
| Input | Keyboard + SDL gamepad support, per-player configs (up to 8 players) |
| Mouse | Mouse input for UI and in-game mouse support |
| Screenshots | F12 saves BMP screenshots to the Pictures folder |
| Modding | `/mods/<game_id>` sprite, map, and Lua patches |

### Platform integration

| Area | Details |
| --- | --- |
| Storage | Uses XDG data and config folders (`~/.local/share/real8` and `~/.config/real8`) |
| Repo downloads | Uses `curl` or `wget` if available on PATH |
| Logging | Writes `logs.txt` to the data directory |
| Clipboard | Reads from the system clipboard |

## Supported cart formats

| Format | Notes |
| --- | --- |
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## Data paths

| Path | Purpose |
| --- | --- |
| `$XDG_DATA_HOME/real8` or `~/.local/share/real8` | Carts, saves, mods, screenshots fallback, logs |
| `$XDG_CONFIG_HOME/real8` or `~/.config/real8` | `config.dat`, `wallpaper.png`, `favorites.txt`, `gameslist.json`, `gamesrepo.txt` |
| `$XDG_PICTURES_DIR/Real8 Screenshots` or `~/Pictures/Real8 Screenshots` | Screenshot output (when available) |

## Build requirements

| Requirement | Notes |
| --- | --- |
| CMake 3.10+ | Build system |
| C++17 compiler | GCC or Clang |
| SDL2 development headers | `libsdl2-dev` / `SDL2-devel` |

## Toolchain setup (Linux)

Install dependencies for your distro:

Ubuntu / Debian:

```sh
sudo apt update
sudo apt install cmake g++ libsdl2-dev
```

Fedora:

```sh
sudo dnf install cmake gcc-c++ SDL2-devel
```

Arch:

```sh
sudo pacman -S cmake gcc sdl2
```

## Build steps (CMake)

From the repository root:

```sh
cmake -S src/platforms/linux -B build/linux
cmake --build build/linux
```

Optional Ninja build:

```sh
cmake -S src/platforms/linux -B build/linux -G Ninja
cmake --build build/linux
```

The output binary is `build/linux/Real8Linux`.

## Usage

### Launch

```sh
./build/linux/Real8Linux
```

### Load a cart

- Drag and drop a `.p8` or `.png` file onto the window, or
- Launch with a path argument:

```sh
./build/linux/Real8Linux /path/to/cart.p8
```

### Use a remote repo

- Put a URL in `gamesrepo.txt` inside the config directory, or
- Place `gameslist.json` in the config directory

### Fullscreen and screenshots

- `F11` toggles fullscreen
- `Esc` exits fullscreen
- `F12` captures a screenshot

### Save and load state

- Use the Shell menu save/load entries

## Screenshots

Linux screenshots are saved via `F12` to the Pictures folder listed above.

## Notes and limitations

- External gamepad config UI is not supported.
- Rename UI is not supported yet.
- Remote repo downloads require `curl` or `wget` on PATH.
