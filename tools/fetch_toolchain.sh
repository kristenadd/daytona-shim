#!/bin/sh
# Unpacks llvm-mingw (macOS universal or Linux x86_64) into build/toolchains.
set -e
cd "$(dirname "$0")/.."
tag=${LLVM_MINGW_TAG:-20261117}
case "$(uname -s)" in
  Darwin) host=macos-universal ;;
  Linux) host=ubuntu-22.04-x86_64 ;;
  *) echo "unsupported host" >&2; exit 1 ;;
esac
name=llvm-mingw-$tag-ucrt-$host
mkdir -p build/toolchains
[ -d build/toolchains/$name ] && { echo "build/toolchains/$name"; exit 0; }
curl -fL -o build/toolchains/$name.tar.xz \
  https://github.com/mstorsjo/llvm-mingw/releases/download/$tag/$name.tar.xz
tar -C build/toolchains -xf build/toolchains/$name.tar.xz
rm build/toolchains/$name.tar.xz
echo "build/toolchains/$name"
