#!/bin/sh
# Unpacks SDL3's official MinGW development release (headers, and the 32-bit
# SDL3.dll the package ships beside dinput8.dll) into build/third_party.
set -e
cd "$(dirname "$0")/.."
ver=${SDL_VERSION:-3.4.20}
dir=build/third_party/SDL3-$ver
mkdir -p build/third_party
[ -d $dir ] && { echo "$dir"; exit 0; }
curl -fL -o build/third_party/sdl.tar.gz \
  https://github.com/libsdl-org/SDL/releases/download/release-$ver/SDL3-devel-$ver-mingw.tar.gz
tar -C build/third_party -xzf build/third_party/sdl.tar.gz
rm build/third_party/sdl.tar.gz
echo "$dir"
