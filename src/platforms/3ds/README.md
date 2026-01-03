# REAL-8 Explorer for Nintendo 3DS

REAL-8 Explorer for Nintendo 3DS is the citro2d/citro3d host for the Real8 VM + Shell. It provides a dual-screen front-end for browsing and running PICO-8 carts on 3DS.

> Not affiliated with Lexaloffle Games, PICO-8, or Nintendo.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [SD card layout](#sd-card-layout)
- [Build requirements](#build-requirements)
- [Toolchain setup (devkitPro)](#toolchain-setup-devkitpro)
- [Build steps (Makefile)](#build-steps-makefile)
- [Usage](#usage)
- [Screenshots](#screenshots)
- [Notes and limitations](#notes-and-limitations)

## Overview

- 3DS host for the Real8 VM + Shell (libctru, citro2d, citro3d).
- Top screen for gameplay, bottom screen for UI/preview and touch input.
- Optimized rendering path with GPU paletted textures where supported.

## Features

### Core experience

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from `sdmc:/real8` |
| Library and browsing | Local library, favorites, repository URL support, remote games with folders and genre |
| Save states | Save and load VM state from the Shell menu |
| Wallpaper | Custom `wallpaper.png` with auto-seeded default from RomFS |
| Video | Dual-screen scaling, stretch mode, interpolation toggle |
| Effects | Optional CRT scanline filter |
| Audio | NDSP audio output (DSP firmware required) |
| Input | 3DS buttons, touchscreen mapped to the in-game mouse |
| Screenshots | BMP screenshots saved to `sdmc:/real8/screenshots` |
| Modding | `/mods/<game_id>` sprite, map, and Lua patches |
| Rename | On-device software keyboard for renaming carts |

### Platform integration

| Area | Details |
| --- | --- |
| Storage | Auto-creates `real8`, `config`, `saves`, `mods`, and `screenshots` folders on the SD card |
| Config seeding | Copies `wallpaper.png` and `gamesrepo.txt` from RomFS if missing |
| Repo downloads | HTTP/HTTPS downloads via HTTPC with redirect handling |
| Logging | Console logs plus `sdmc:/real8/log.txt` for important errors |

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
| `sdmc:/real8/log.txt` | Error and debug log (important lines only) |

## Build requirements

| Requirement | Notes |
| --- | --- |
| devkitPro + devkitARM | 3DS toolchain and libctru |
| 3ds-dev | citro2d/citro3d + 3DS libraries |
| 3dstools | `3dstool`, `tex3ds` |
| bannertool + makerom | Required for CIA builds |

## Toolchain setup (devkitPro)

1) Install devkitPro (includes MSYS2):
   https://devkitpro.org/wiki/Getting_Started

2) Open the devkitPro MSYS2 shell:
   `C:\msys64\msys2.exe`

3) Update and install the 3DS packages:

```sh
pacman -Syu
pacman -S --needed 3ds-dev 3dstools bannertool makerom
```

4) Ensure your environment has `DEVKITARM` set:

```sh
echo $DEVKITARM
```

## Build steps (Makefile)

From `src/platforms/3ds`:

```sh
make
```

This produces `REAL8.3dsx`.

Optional CIA build:

```sh
make cia
```

## Usage

### Load local carts

- Copy `.p8` or `.png` carts into `sdmc:/real8`
- Launch the `.3dsx` from the Homebrew Menu (or install the CIA)

### Use a remote repo

- Put a URL in `sdmc:/real8/config/gamesrepo.txt`, or
- Place `gameslist.json` in `sdmc:/real8/config`

### Save and load state

- Use the Shell menu save/load entries

### Exit

- Hold Start + Select to quit

## Screenshots

### REAL-8 on 3DS

![REAL-8 on 3DS](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-3DS.png)

### Default wallpaper

![Default wallpaper](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/src/platforms/3ds/romfs/wallpaper.png)

## Notes and limitations

- Audio requires DSP firmware at `sdmc:/3ds/dspfirm.cdc` (install via DSP1 or 3ds.hacks.guide).
- External gamepad config UI is not available; use the internal menu or edit `config.dat`.
- Performance varies; some carts may run slower on 3DS.
