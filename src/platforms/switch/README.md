# REAL-8 Explorer for Nintendo Switch

REAL-8 Explorer for Nintendo Switch is the libnx host for the Real8 VM + Shell. It provides a native fullscreen SDL2 front-end for browsing and running PICO-8 carts on Switch.

> Not affiliated with Lexaloffle Games, PICO-8, or Nintendo.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [SD card layout](#sd-card-layout)
- [Build requirements](#build-requirements)
- [Toolchain setup (devkitPro)](#toolchain-setup-devkitpro)
- [Build steps (CMake)](#build-steps-cmake)
- [Usage](#usage)
- [Screenshots](#screenshots)
- [Notes and limitations](#notes-and-limitations)

## Overview

- Switch host for the Real8 VM + Shell (libnx + SDL2).
- Runs in handheld or docked mode at 1280x720.
- Tested on FW 21.1.0 with AMS 1.10.1.

## Features

### Core experience

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from `sdmc:/real8` |
| Library and browsing | Local library, favorites, repository URL support, remote games with folders and genre |
| Save states | Save and load VM state from the Shell menu |
| Wallpaper | Custom `wallpaper.png` with auto-seeded default from ROMFS |
| Video | Fullscreen 720p output, stretch modes, interpolation toggle |
| Effects | Optional CRT scanline filter |
| Audio | SDL2 queued audio output |
| Input | Joy-Con, Pro Controller, handheld; up to 8 players; analog sticks map to D-pad |
| Touch | Touchscreen mapped to the in-game mouse |
| Screenshots | BMP screenshots saved to `sdmc:/real8/screenshots` |
| Modding | `/mods/<game_id>` sprite, map, and Lua patches |
| Rename | On-device software keyboard for renaming carts |

### Platform integration

| Area | Details |
| --- | --- |
| Storage | Auto-creates `real8`, `config`, `saves`, `mods`, and `screenshots` folders on the SD card |
| Repo downloads | HTTP/HTTPS downloads via libcurl (used for remote repo lists) |
| Network info | Reads connection status and IP via NIFM when available |
| Logging | `printf` output viewable via nxlink |

## Supported cart formats

| Format | Notes |
| --- | --- |
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## SD card layout

| Path | Purpose |
| --- | --- |
| `sdmc:/real8` | Carts placed here show up as local games |
| `sdmc:/real8/config` | `config.dat`, `gamesrepo.txt`, `gameslist.json`, `wallpaper.png` |
| `sdmc:/real8/saves` | Save states (`.sav`) |
| `sdmc:/real8/mods` | Mod packs by game id |
| `sdmc:/real8/screenshots` | Captured screenshots |

## Build requirements

| Requirement | Notes |
| --- | --- |
| devkitPro + devkitA64 | Switch toolchain and libnx |
| CMake 3.10+ | Build system |
| switch-sdl2 | SDL2 for Switch |
| switch-zlib | Zlib |
| switch-curl | libcurl |
| switch-tools | `elf2nro` and `nacptool` |
| dkp-cmake-common-utils | devkitPro CMake helpers |
| devkita64-cmake, switch-cmake | Switch CMake toolchain files |
| dkp-toolchain-vars, devkit-env | Environment setup |

## Toolchain setup (devkitPro)

1) Install devkitPro (includes MSYS2):
   https://devkitpro.org/wiki/Getting_Started

2) Open the devkitPro MSYS2 shell:
   `C:\msys64\msys2.exe`

3) Update and install the Switch packages:

```sh
pacman -Syu
pacman -S --needed switch-sdl2 switch-zlib switch-curl switch-tools dkp-cmake-common-utils devkita64-cmake switch-cmake dkp-toolchain-vars devkit-env make
```

4) If pacman keyring errors appear, reset and re-sync:

```sh
rm -rf /etc/pacman.d/gnupg
pacman-key --init
pacman-key --populate
pacman-key --recv-keys 62C7609ADA219C60 F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
pacman-key --lsign-key 62C7609ADA219C60
pacman-key --lsign-key F7FD5492264BB9D0
rm -f /var/lib/pacman/sync/dkp-libs* /var/lib/pacman/sync/dkp-windows*
pacman -Syu
```

5) Ensure `DEVKITPRO` is set:

```sh
export DEVKITPRO=/c/devkitPro
```

## Build steps (CMake)

From the repository root:

```sh
cmake -S src/platforms/switch -B build/switch -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=/c/devkitPro/cmake/Switch.cmake
cmake --build build/switch
```

The output `Real8Switch.nro` is generated in `build/switch`.

## Usage

### Load local carts

- Copy `.p8` or `.png` carts into `sdmc:/real8`
- Launch the NRO from the Homebrew Menu

### Use a remote repo

- Put a URL in `sdmc:/real8/config/gamesrepo.txt`, or
- Place `gameslist.json` in `sdmc:/real8/config`

### Save and load state

- Use the Shell menu save/load entries

### Exit

- Press Minus (-) to quit back to the Homebrew Menu

## Screenshots

### REAL-8 on Switch

![REAL-8 on Switch](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8Switch.png)

### Default wallpaper

![Default wallpaper](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/src/platforms/switch/romfs/real8/config/wallpaper.png)

## Notes and limitations

- No system clipboard integration.
- External gamepad config UI is not available; use the internal menu or edit `config.dat`.
- Debug output is available through nxlink.
