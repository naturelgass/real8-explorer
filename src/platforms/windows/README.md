# REAL-8 Explorer for Windows

REAL-8 Explorer for Windows is the Windows host of the REAL-8 Browser. It provides a native SDL-based window, menus, input, and tooling around the core Real8 VM and Shell so you can browse, load, and run REAL-8 carts on Windows.

## Features

- Windowed, resizable SDL host with fullscreen toggle, Stretch and drag-and-drop cart loading
- Native Windows menu bar with File / Options / Settings / Effects / Extra actions
- Load carts from disk (.p8 / .png) and set a games repository URL
- Shell browser controls for showing repo games, repo snapshots, and local snapshots
- Save and load VM state
- Screenshot capture to Pictures\Real8 Screenshots (or project screenshots folder)
- Wallpaper support with import, scaling, and on-disk persistence
- Rendering options: CRT scanline filter and interpolation toggle
- Audio controls: music and SFX toggles with queued-audio sync
- Input: keyboard + SDL gamepad support with a remap UI and per-player configs
- Debug Console with logging, pause/resume, step, breakpoints, memory tools
- Real-time modding window with live variable editing, favorites, and command prompt
- Export tools: GFX, MAP, and music tracks to a chosen folder
- Crash handler that logs exceptions to logs.txt and shows a Windows dialog

## Not yet implemented (Windows host)

- Network configuration is stubbed: setWifiCredentials and setNetworkActive are no-ops, and network info is fixed to localhost/desktop mode
- Overlay updates are unimplemented (updateOverlay is empty)
- Storage reporting uses placeholder values (used = 0, total = 1GB)
- Export Vars menu item is defined but not wired into the Windows menu

## Scope

This README describes the Windows host only. Core VM, Shell, and cross-platform features live elsewhere in the REAL-8 Browser codebase.
