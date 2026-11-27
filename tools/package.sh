#!/bin/sh
# Builds the 32-bit DLL and makes build/dist/daytona-shim.zip.
set -e
cd "$(dirname "$0")/.."
tools/fetch_toolchain.sh >/dev/null
sdl=$(tools/fetch_sdl.sh)
cmake -S . -B build/win32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/win32
rm -rf build/dist/daytona-shim && mkdir -p build/dist/daytona-shim
cp build/win32/dinput8.dll build/win32/DaytonaLauncher.exe dist/INSTALL.txt dist/shim.ini build/dist/daytona-shim/
cp $sdl/i686-w64-mingw32/bin/SDL3.dll build/dist/daytona-shim/
cp $sdl/LICENSE.txt build/dist/daytona-shim/SDL3-LICENSE.txt
(cd build/dist && rm -f daytona-shim.zip && zip -qr daytona-shim.zip daytona-shim)
ls -l build/dist/daytona-shim.zip
