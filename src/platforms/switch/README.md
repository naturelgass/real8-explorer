# Install devkitProUpdater
# Run C:\msys64\msys2.exe

# --- (Optional but often helps) reset pacman's keyring ---

rm -rf /etc/pacman.d/gnupg
pacman-key --init
pacman-key --populate

# --- Import + locally trust the devkitPro keys ---

pacman-key --recv-keys 62C7609ADA219C60 F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
pacman-key --lsign-key 62C7609ADA219C60
pacman-key --lsign-key F7FD5492264BB9D0

# Delete the now-"bad" repo DB files and re-sync
rm -f /var/lib/pacman/sync/dkp-libs* /var/lib/pacman/sync/dkp-windows*

# --- switch-sdl2 ---

pacman -Syu
pacman -S --needed switch-sdl2

# --- switch-zlib ---

pacman -Syu
pacman -S --needed switch-zlib

# --- switch-curl ---

pacman -Syu
pacman -S --needed switch-curl

# toolchain/cmake bits that provide dkp-toolchain-common + Switch CMake files

pacman -Syu
pacman -S --needed dkp-cmake-common-utils devkita64-cmake switch-cmake dkp-toolchain-vars devkit-env

# -- (Optional if needed) ---
pacman -S --needed make

# Navigate to Build folder /switch/build/

# --- Update the variable to point to the REAL location ---
export DEVKITPRO=/c/devkitPro

# --- Clear the old broken configuration  ---
rm -rf *

# --- Run CMake using the physical path  ---
cmake -DCMAKE_TOOLCHAIN_FILE=/c/devkitPro/cmake/Switch.cmake -G "Unix Makefiles" ..

# --- Compile ---
make