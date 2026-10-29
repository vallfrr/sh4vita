#!/bin/bash
# Builds then runs the PC build without a dialog box, with a maximum delay (s).
# Usage : bash port/run_pc.sh [secondes]
set -u
cd "$(dirname "$0")"
export PATH="/j/tools/mingw64/bin:$PATH"
mingw32-make -f Makefile.pc -j16 SHELL=sh > ../build/pc/build.log 2>&1 || { grep -E 'error|undefined' ../build/pc/build.log | head -30; exit 1; }
cd ../build/pc
rm -f sh4log.txt
SH4_HEADLESS=1 timeout "${1:-30}" ./sh4.exe "D:/Games/Silent Hill 4"
echo "exit=$?"
tail -n 40 sh4log.txt
