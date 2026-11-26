#!/bin/sh
# Builds the 32-bit DLL and makes build/dist/daytona-shim.zip.
set -e
cd "$(dirname "$0")/.."
tools/fetch_toolchain.sh >/dev/null
cmake -S . -B build/win32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/win32
rm -rf build/dist/daytona-shim && mkdir -p build/dist/daytona-shim
cp build/win32/dinput8.dll dist/INSTALL.txt build/dist/daytona-shim/
(cd build/dist && rm -f daytona-shim.zip && zip -qr daytona-shim.zip daytona-shim)
ls -l build/dist/daytona-shim.zip
