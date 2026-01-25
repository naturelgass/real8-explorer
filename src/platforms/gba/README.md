# GBA Host (PICO-8 to GBA)

This port packages a single PICO-8 cart into a standalone GBA ROM and runs it on the Real8 VM.
It focuses on speed and memory, so it is optimized to run on a real GBA hardware.

## Table of contents

- [How it works](#how-it-works)
- [Memory layout (ROM, IWRAM, EWRAM)](#memory-layout-rom-iwram-ewram)
- [Constraints](#constraints)
- [Optimizations in this port](#optimizations-in-this-port)
- [PICO-8 performance tips](#pico-8-performance-tips)
- [Build requirements](#build-requirements)
- [Build steps](#build-steps)
- [Pico2GBA GUI (Windows)](#pico2gba-gui-windows)

### PICO-8 on GAME BOY Advance
![Real8 on GBA](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Real8-Pico2GBA-Rom.png)

## How it works

| Stage | Tooling | Output |
| --- | --- | --- |
| Pack cart | `cart_packer` or `pico2gba` GUI | `cart_blob.bin` |
| Build ROM | `make` + devkitARM | `REAL8_GBA.gba` |

Runtime flow:
- The cart blob is embedded in ROM and loaded at boot (no shell).
- The Real8 VM renders a 128x128 framebuffer.
- The host blits to Mode 4 (240x160, 8bpp) with a centered viewport.
- Input maps D-pad + A/B, Start/Select; L/R mirror A/B.

## Memory layout (ROM, IWRAM, EWRAM)

| Region | Size | Used for in this port | Notes |
| --- | --- | --- | --- |
| ROM | cart space | executable, assets, `cart_blob.bin` | inspect `REAL8_GBA.map` for usage |
| IWRAM | 32 KB | hot loops (gfx, input, JIT) | `.iwram` / `REAL8_GBA_IWRAM_*` |
| EWRAM | 256 KB | VM state, framebuffer, LUTs, cart data | `.ewram` / `REAL8_GBA_*_SECTION` |

Tip: the linker runs with `--print-memory-usage` so builds report ROM/IWRAM/EWRAM totals.

## Constraints

| Area | Constraint | Impact |
| --- | --- | --- |
| CPU | ARM7TDMI ~16.78 MHz, no FPU | avoid heavy per-pixel loops and float math |
| RAM | 32 KB IWRAM + 256 KB EWRAM | big Lua tables and buffers can stall |
| Video | Mode 4 (240x160, 8bpp) | 128x128 framebuffer is centered with borders |
| Audio | disabled by default (`REAL8_GBA_ENABLE_AUDIO=0`) | enable only if needed; costs CPU |

## Optimizations in this port

| Optimization | Where | Notes |
| --- | --- | --- |
| Baseline Lua JIT | `LUA_GBA_BASELINE_JIT` | enabled by default; lowers CPU per opcode |
| JIT in IWRAM | `REAL8_GBA_JIT_IWRAM=1` | faster, uses IWRAM budget |
| IWRAM hot paths | `.iwram` sections | gfx primitives, input, blit, map, sprite |
| Dirty-rect + tile hashing | `gba_host.cpp` | update only changed tiles |
| Sprite batching | `GbaHost::queueSprite` | reduces per-sprite overhead |
| Skip VBlank (optional) | `REAL8_GBA_SKIP_VBLANK` | more CPU, potential tearing |

## PICO-8 performance tips

- Prefer `spr`/`sspr`/`map` over large `pset`/`line` loops.
- Minimize full-screen redraws; redraw only what changed each frame.
- Keep sprite counts and `sspr` scaling modest.
- Avoid heavy use of `sin`/`cos` in tight loops; precompute tables when possible.
- Reuse Lua tables; avoid per-frame allocations and big string ops.
- Limit `pal`/`palt` changes per frame; batch draws with the same palette.
- Keep map draws to visible bounds; avoid drawing huge map regions offscreen.
- If audio is enabled, keep music/SFX simple and avoid dense SFX spam.

## Build requirements

| Requirement | Purpose | Notes |
| --- | --- | --- |
| devkitPro (devkitARM + libgba) | GBA toolchain | set `DEVKITPRO` and `DEVKITARM` |
| devkitPro tools | image/bin conversion | `grit` and `bin2s` come with devkitPro |
| Host C++ compiler | build cart packer/GUI | `g++` or clang; on Windows use MSYS2/MinGW |
| GNU make | build system | required for the Makefile |
| `windres` (Windows only) | GUI resources | used for `pico2gba` |

## Build steps

1. Install devkitPro with devkitARM + libgba, then set environment variables:
You will need to install the official msys64 as well - and use "mingw64.exe" for terminal 
```sh
DEVKITPRO=c:/devkitPro
DEVKITARM=$DEVKITPRO/devkitARM
```

2. Ensure a host compiler (`g++`) and `make` are in your PATH.
3. `cd src/platforms/gba`
4. Put your cart image next to the Makefile as `game.p8.png`.
5. Build tools and ROM:

```sh
make clean
make
```

7. On Windows, if your shell has issues with POSIX commands, run:

```sh
make REAL8_HOST_CMD=1
```

Outputs include `REAL8_GBA.gba` (ROM), plus `REAL8_GBA.elf` and `REAL8_GBA.map` for debugging and size info.

## Pico2GBA GUI (Windows)

The GUI wraps the Makefile build and drops the ROM next to your cart.

1. Build the GUI (once):

```sh
make template
# outputs: REAL8_GBA_template.gba
```

```sh
make template CART_TEMPLATE_CAPACITY=524288
# (Optional) To increase the cart slot size
```

Place REAL8_GBA_template.gba next to Pico2GBA.exe (or browse to it in Step 1), select a .p8.png, hit Generate → it outputs a .gba.


```sh
cd src/platforms/gba
make tools
```

1. Launch `build/Pico2GBA.exe`.
2. Select PICO-8 game file and optional Skin/Background.
3. Optional: adjust feature toggles (Audio, Fast Lua, Skip VBlank, Profile GBA).
4. Click **Generate**.

Outputs:
- The ROM is written next to your cart as `cartname.gba`.
- A build log is saved to `build/pico2gba_build.log`.
- Last-used paths are cached in `build/pico2gba.ini`.

### Pico2GBA GUI Dialog
![Real8 - Pico2GBA](https://raw.githubusercontent.com/naturelgass/real8-explorer/refs/heads/main/screenshots/Pico2GBA.png)

