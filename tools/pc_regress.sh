#!/bin/bash
# PC non-regression: bash tools/pc_regress.sh [sync|lazy|thread]  (compares against build/ref_head)
cd /j/sh4vita/port && export PATH="/j/tools/mingw64/bin:$PATH"
mingw32-make -f Makefile.pc -j16 SHELL=sh > ../build/pc/build.log 2>&1 || { grep -E 'error|undefined' ../build/pc/build.log | head -30; exit 1; }
cd ../build/pc && rm -f game/save/sh4.sav* frame_*.bmp sh4log.txt
SH4_PREP=${1:-sync} SH4_VBO=1 SH4_VTIME=1 SH4_HEADLESS=1 SH4_RES=0 SH4_DUMP=250 SH4_STATS=1 SH4_WARP=3700:3 \
 SH4_INPUT="$(tr -d '\r\n' < ../../tools/pc_reference_route.txt)" timeout 250 ./sh4.exe "J:/sh4vita/build/pc/game"
echo exit=$?
n=0; bad=0; for f in ../ref_head/frame_*.bmp; do b=$(basename $f); n=$((n+1)); cmp -s $f $b || { bad=$((bad+1)); echo DIFF $b; }; done
echo "frames=$n diff=$bad"
tail -n 15 sh4log.txt
