#!/bin/bash
cd /j/sh4vita/port && export PATH="/j/tools/mingw64/bin:$PATH"
mingw32-make -f Makefile.pc -j16 SHELL=sh > ../build/pc/build.log 2>&1 || { grep -E 'error|undefined' ../build/pc/build.log | head -30; exit 1; }
cd ../build/pc && rm -f game/save/sh4.sav* frame_*.bmp sh4log.txt
SH4_VBO=1 SH4_VTIME=1 SH4_HEADLESS=1 SH4_RES=0 SH4_DUMP=250 SH4_STATS=1 SH4_WARP=3700:3 \
 SH4_INPUT="$(tr -d '\r\n' < ../../tools/pc_reference_route.txt)" timeout ${1:-250} ./sh4.exe "J:/sh4vita/build/pc/game"
echo exit=$?
