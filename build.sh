#!/bin/sh
# Build pbterm for PocketBook (SDK 6.5, branch "6.5" of github.com/pocketbook/SDK_6.3.0)
# Usage: SDK=/path/to/SDK65/SDK-B288 ./build.sh
set -e
SDK=${SDK:?set SDK to the SDK-B288 directory}
export LD_LIBRARY_PATH="$SDK/usr/lib"          # libmpfr.so.4 etc. shipped with the toolchain
CXX="$SDK/usr/bin/arm-obreey-linux-gnueabi-g++"
SYS="$SDK/usr/arm-obreey-linux-gnueabi/sysroot"
mkdir -p build
for f in src/*.cpp; do
    "$CXX" -c -O2 -fsigned-char -Wall -Wextra -Isrc "$f" -o "build/$(basename "$f" .cpp).o"
done
"$CXX" -o build/pbterm.app build/*.o -L"$SYS/usr/local/lib" -linkview -lpthread -ldl
"$SDK/usr/bin/arm-obreey-linux-gnueabi-strip" build/pbterm.app
echo "OK: build/pbterm.app"
