#!/bin/bash
# Runs the PC build under gdb and prints the stack at the first crash.
# Usage: bash port/gdb_pc.sh [extra gdb commands...]
cd "$(dirname "$0")/../build/pc"
export PATH="/j/tools/mingw64/bin:$PATH"
SH4_HEADLESS=1 timeout 300 gdb -batch -ex run -ex bt -ex "info registers rip" "$@" \
    --args ./sh4.exe "D:/Games/Silent Hill 4" 2>&1 | grep -v '^\[New Thread\|^\[Thread\|Windows.Gaming.Input\|^warning: onecore' | tail -40
