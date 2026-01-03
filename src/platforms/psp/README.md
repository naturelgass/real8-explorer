# REAL-8 Explorer for Sony PSP

REAL-8 Explorer for PSP is the native host for the Real8 VM + Shell. It provides a fullscreen GU front-end for browsing and running PICO-8 carts on PSP.

> Not affiliated with Lexaloffle Games, PICO-8, or Sony.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Memory Stick layout](#memory-stick-layout)
- [Build requirements](#build-requirements)
- [Toolchain setup (pspdev)](#toolchain-setup-pspdev)
- [Build steps (Makefile)](#build-steps-makefile)
- [Usage](#usage)
- [Notes and limitations](#notes-and-limitations)

## Overview

- PSP host for the Real8 VM + Shell (PSP GU + audio).
- 480x272 output with stretch and interpolation options.
- Runs from `ms0:/PSP/GAME/REAL8`.

## Features

### Core experience

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from `ms0:/PSP/GAME/REAL8` |
| Library and browsing | Local library and favorites |
| Save states | Save and load VM state from the Shell menu |
| Wallpaper | Custom `wallpaper.png` seeded from embedded data or PBP assets |
| Video | Fullscreen 480x272 output, stretch mode, interpolation toggle |
| Effects | Optional CRT scanline filter |
| Audio | PSP audio output with real-time resampling |
| Input | D-pad + analog stick, buttons mapped to PICO-8 controls |
| Fast-forward | Hold R trigger |
| Modding | `/mods/<game_id>` sprite, map, and Lua patches |

### Platform integration

| Area | Details |
| --- | --- |
| Storage | Auto-creates `real8`, `config`, `saves`, `mods`, and `screenshots` folders |
| Wallpaper seeding | Uses embedded `wallpaper.png` or extracts `PIC1.PNG` from `EBOOT.PBP` |
| Logging | Console logging via `printf` |

## Supported cart formats

| Format | Notes |
| --- | --- |
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## Memory Stick layout

| Path | Purpose |
| --- | --- |
| `ms0:/PSP/GAME/REAL8` | Carts placed here show up as local games |
| `ms0:/PSP/GAME/REAL8/config` | `config.dat`, `gamesrepo.txt`, `gameslist.json`, `wallpaper.png` |
| `ms0:/PSP/GAME/REAL8/saves` | Save states (`.sav`) |
| `ms0:/PSP/GAME/REAL8/mods` | Mod packs by game id |
| `ms0:/PSP/GAME/REAL8/screenshots` | Placeholder folder (screenshots not supported yet) |

## Build requirements

| Requirement | Notes |
| --- | --- |
| PSP SDK (pspdev) | Toolchain providing `psp-config` and PSP headers |
| GNU Make | Build system |

## Toolchain setup (pspdev)

1) Install the PSP toolchain (pspdev / psptoolchain):
   https://github.com/pspdev/psptoolchain

2) Ensure `psp-config` is in your PATH:

```sh
psp-config --pspsdk-path
```

3) Verify `PSPSDK` / `PSPDEV` are set (psptoolchain does this automatically).

## Build steps (Makefile)

From `src/platforms/psp`:

```sh
make
```

This produces `EBOOT.PBP` in the same folder.

## Usage

### Load local carts

- Copy `.p8` or `.png` carts into `ms0:/PSP/GAME/REAL8`
- Launch the game from the PSP XMB

### Save and load state

- Use the Shell menu save/load entries

### Exit

- Use the in-app quit option, or press the PSP HOME button

## Notes and limitations

- Network downloads are not available on PSP (offline only).
- External gamepad config UI is not supported.
- Rename UI is not supported yet.
- Screenshots are not supported yet.
