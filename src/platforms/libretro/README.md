# REAL-8 Explorer for Libretro

REAL-8 Explorer for Libretro is a Libretro core built around the Real8 VM. It runs PICO-8 carts inside RetroArch or any Libretro frontend.

> Not affiliated with Lexaloffle Games, PICO-8, or Libretro.

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Supported cart formats](#supported-cart-formats)
- [Build requirements](#build-requirements)
- [Toolchain setup (Libretro)](#toolchain-setup-libretro)
- [Build steps (Makefile)](#build-steps-makefile)
- [Usage](#usage)
- [Notes and limitations](#notes-and-limitations)

## Overview

- Libretro core for the Real8 VM (128x128 framebuffer, 60fps, 22050 Hz audio).
- Designed for RetroArch or other Libretro frontends.
- Focused on running a single cart loaded by the frontend.

## Features

### Core runtime

| Area | Details |
| --- | --- |
| Cart loading | Load `.p8` and `.png` carts from the frontend |
| Video | 128x128 output, XRGB8888 pixel format |
| Audio | Mono VM audio converted to stereo for Libretro |
| Input | RetroPad mapping for up to 2 players |
| Save states | Supported via RetroArch serialization |

### Libretro integration

| Area | Details |
| --- | --- |
| Input descriptors | Button names exposed to the frontend |
| Controller types | RetroPad registered for ports 1-2 |
| Core options | None (no options menu) |
| Cheats | Not supported |

## Supported cart formats

| Format | Notes |
| --- | --- |
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## Build requirements

| Requirement | Notes |
| --- | --- |
| C++17 compiler | GCC, Clang, or MinGW-w64 |
| GNU Make | Build system |
| libretro headers | `libretro.h` (included in this folder) |

## Toolchain setup (Libretro)

Install a compiler toolchain for your OS:

Linux (Debian/Ubuntu):

```sh
sudo apt update
sudo apt install build-essential make
```

macOS:

```sh
xcode-select --install
```

Windows (MinGW-w64):

```sh
pacman -S --needed mingw-w64-x86_64-toolchain make
```

## Build steps (Makefile)

From `src/platforms/libretro`:

```sh
make
```

Platform targets:

```sh
make platform=unix   # .so
make platform=win    # .dll
make platform=osx    # .dylib
```

Release size options (optional):

```sh
make OPTIMIZE_SIZE=1 USE_LTO=1 STRIP_DLL=1
```

The output files are:

| Platform | Output |
| --- | --- |
| Linux | `real8_libretro.so` |
| Windows | `real8_libretro.dll` |
| macOS | `real8_libretro.dylib` |

## Usage

1) Copy the built core into your frontend's cores directory.
2) Copy `real8_libretro.info` into RetroArch's `info` directory.
3) In RetroArch: Load Core -> select the Real8 core.
4) Load Content -> select a `.p8` or `.png` cart.

## Notes and limitations

- No in-core UI or repo browser; the frontend handles content selection.
- Save states are provided by RetroArch (no battery save implementation).
- Network downloads and external gamepad config UI are not supported.
