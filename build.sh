#!/usr/bin/env bash
# Build ArcTray (x64, no runtime deps). Requires mingw-w64 gcc (Strawberry Perl ships one).
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-x86_64-w64-mingw32-gcc}
CFLAGS="-Os -std=c11 -ffunction-sections -fdata-sections -fno-ident -Wall -Wextra -Wno-unused-parameter"
LDFLAGS="-Wl,--gc-sections -static -s -luser32 -lshell32 -lole32"
python tools/make_icon.py
$CC $CFLAGS -o arctray.exe src/tray.c src/arc.c src/tweaks.c src/gfx.c $LDFLAGS
cp -f res/arc.ico arc.ico
ls -la arctray.exe
