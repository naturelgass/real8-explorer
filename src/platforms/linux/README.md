# Linux Standalone (SDL2)

Requirements:
- CMake and a C++17 compiler (g++ or clang++)
- SDL2 development headers (libsdl2-dev)

Build:
- rm -rf build
- cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/usr/local
- cmake --build build

Run:
- ./build/Real8Linux

Games Load:
- Select a game from REPO
- Drag and drop carts onto the window to load them

Data paths:
- Data: ~/.local/share/real8
- Config: ~/.config/real8
- Repo downloads use curl or wget if present on PATH
