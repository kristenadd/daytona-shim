#!/bin/sh
# The launcher's build inputs, pinned: stb_truetype and stb_image (public
# domain / MIT) and two OFL fonts, Russo One and Titillium Web, into
# build/third_party/assets. Nothing here is committed.
set -e
cd "$(dirname "$0")/.."
dir=build/third_party/assets
mkdir -p $dir
stb=6b5799bae8a1fe3792691ef4b72ae26434c3e799
fonts=5026800714d07231937d92eb4cceabf06e05d74e
get() { [ -s "$dir/$2" ] || curl -fsSL -o "$dir/$2" "$1"; }
get https://raw.githubusercontent.com/nothings/stb/$stb/stb_truetype.h stb_truetype.h
get https://raw.githubusercontent.com/nothings/stb/$stb/stb_image.h stb_image.h
get https://raw.githubusercontent.com/google/fonts/$fonts/ofl/russoone/RussoOne-Regular.ttf RussoOne-Regular.ttf
get https://raw.githubusercontent.com/google/fonts/$fonts/ofl/russoone/OFL.txt RussoOne-OFL.txt
get https://raw.githubusercontent.com/google/fonts/$fonts/ofl/titilliumweb/TitilliumWeb-Bold.ttf TitilliumWeb-Bold.ttf
get https://raw.githubusercontent.com/google/fonts/$fonts/ofl/titilliumweb/OFL.txt TitilliumWeb-OFL.txt
echo "$dir"
