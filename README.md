# REAL-8 Explorer

A PICO-8 explorer and emulator front-end for multiple platforms.

REAL-8 Explorer is the comfort layer around the REAL-8 VM + Shell: a native host UI for windowing, menus, input, and tooling, so you can browse, launch, and debug carts with less friction.

Not affiliated with Lexaloffle Games, PICO-8, Nintendo, or RetroArch/Libretro.

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/naturelgass/real8-explorer)

#### Quick links: ####
- [Release notes (v1.1.0)](https://github.com/naturelgass/real8-explorer/releases/tag/real8vm)
- Demo cart: `demos/pico_extended.p8`

## Highlights (v1.1.0)

- Publishing workflow: GUI exporters for **standalone homebrew** on **GBA** (.gba), **3DS** (.3dsx/.cia), and **Switch** (.nro).
- REAL-8 expandability: extended **framebuffer modes**, **stereo depth** pipeline, and **motion sensors**.
- Performance and stability improvements across supported targets.
- VM goal: PICO-8 compatibility by default, with optional enhancements when you want them.

## Platforms

| Target | Host / Integration | Details |
|------:|---------------------|---------|
| **Windows** | SDL host + native menus/tooling | [Windows README](src/platforms/windows/README.md) |
| **Nintendo Switch** | libnx host integration | [Switch README](src/platforms/switch/README.md) |
| **Nintendo 3DS** | citro2d / citro3d | [3DS README](src/platforms/3ds/README.md) |
| **Nintendo GBA** | devkitARM / GBA SDK | [GBA README](src/platforms/gba/README.md) |
| **Libretro** | Libretro API core | [Libretro README](src/platforms/libretro/README.md) |

## Features

### Core

- Cart loading from disk (`.p8` / `.png`), drag-and-drop on desktop
- Library browsing with local folders, genre grouping, and remote repositories
- Save states
- Custom wallpaper support (on/off)
- Video options: fullscreen, stretch modes, interpolation, optional CRT scanlines
- Audio: music and SFX toggles, queued-audio sync
- Input: keyboard and controller support, HID remapping UI, per-player configs (up to 8)
- Modding: boot games with mods created through the Windows version

### REAL-8 expandability

- Extended framebuffer modes: **240x160** / **320x240** / **640x360 x2** (fullscreen behavior depends on platform)
- Stereo depth pipeline: **stereo on 3DS** or red/cyan on other platforms, per-sprite depth buckets
- Motion sensors: **accelerometer and gyroscope** on 3DS and Switch
- Demo: [PICO EXTENDED](demos/pico_extended.p8)

### Publishing workflow

Publish your PICO-8 cart as a standalone homebrew game using the GUI exporters:

- **Pico2GBA**: publish a `.gba` file (optional 240x160 native resolution)
- **PicoTo3DS**: publish `.3dsx` or `.cia` (optional stereo depth, gyro, and 320x240 native resolution)
- **Pico2Switch**: publish a `.nro` file (optional gyro and 640x360 x2 native resolution)

### Developer and power-user tools

- Debug console: logging, pause/resume, step, breakpoints, memory tools
- Real-time modding: live variable editing, favorites, command prompt
- Live LUA injection: PEEK/POKE with console feedback
- Export tools: GFX, MAP, and music tracks to a chosen folder
- Crash handling (Windows): exception logging to `logs.txt` + a Windows dialog

## Supported cart formats

| Format | Notes |
|-------:|------|
| `.p8` | Text cart source |
| `.png` | Image/cart container |

## Usage

### Load a cart (Windows)

- **File -> Open** and select a cart
- Or drag a `.p8` / `.png` file onto the window

### Load a cart (Switch)

- **Local Games**: place games in the `Switch/Real8/` folder
- **Local Repo JSON**: place [gameslist.json](https://github.com/naturelgass/real8games/blob/main/gameslist.json) in `Switch/Real8/Config`
- **Remote Games**: point to a remote JSON in `gamesrepo.txt`

### Save / load state

- Windows: use the menu or the Shell menu
- Switch: use the Shell menu
- RetroArch: use the default RetroArch save system

## Compatibility and known issues

- Supports about 80-90% of PICO-8 games
- Switch builds target FW 21.1.0 | AMS 1.10.1
- 3DS is optimized, but some carts run slower
- Sound engine in some games may not emulate correctly
- Save state reliability varies by game (some carts may load corrupted state)
- Compatibility is good, but difficult titles (Poom, R-Type, etc.) still need work

## GBA implementation

The GBA target packages a single `.p8.png` cart into a standalone GBA ROM and boots it directly (no shell).
It renders a 128x128 framebuffer centered in Mode 4 and leans on IWRAM/EWRAM optimizations to hit speed.
This implementation is optimized to run on real GBA hardware.
See [GBA README](src/platforms/gba/README.md) for build steps, memory layout, and tuning tips.

## Screenshots

<table>
  <tr>
    <td><img src="https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8Switch.png" alt="REAL-8 on Switch" width="420"></td>
    <td><img src="https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-3DS.png" alt="REAL-8 on 3DS" width="420"></td>
  </tr>
  <tr>
    <td><img src="https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-Pico2GBA-Rom.png" alt="REAL-8 on GBA" width="420"></td>
    <td><img src="https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsShellMenu.png" alt="Windows Shell" width="420"></td>
  </tr>
  <tr>
    <td><img src="https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/WindowsDebugConsole.png" alt="Windows Debug Console" width="420"></td>
    <td></td>
  </tr>
</table>

## Toolchains

### Windows (SDL)
- Toolchain: MinGW
- Dependencies: SDL2

### Nintendo Switch (libnx)
- Toolchain: devkitPro + libnx 4.10.0
- Works with FW 21.1.0 | AMS 1.10.1

### Nintendo 3DS (citro2d/citro3d)
- devkitPro + 3ds-dev + 3dstools
- 3dstool (for packing assets)
- Curl (for online fetching)

### Nintendo GBA (devkitARM / GBA SDK)
- devkitPro / devkitARM + GBA SDK
- Pico2GBA GUI packer/converter

### Libretro
- Libretro toolchain / RetroArch environment

## Contributing

When reporting bugs, please include:
- Target (Windows / Switch / 3DS / GBA / Libretro)
- Cart format (`.p8` / `.png`) and a repro case
- Logs (for example, `logs.txt` on Windows) if available

## License

This project is licensed under **GPL-3.0** (see `LICENSE`).

REAL-8 started as a backronym ("Real-Time Emulation Analog Layer"), but it stuck.