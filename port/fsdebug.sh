#!/bin/bash
# Rendering test: plays cutscene 0 with a fragment-shader debug mode and prints the
# colour of a few pixels of the last saved frame. Usage: bash port/fsdebug.sh uv|diffuse|tex|none
cd "$(dirname "$0")/../build/pc" || exit 1
rm -f frame_*.bmp sh4log.txt
SH4_FSDEBUG="$1" SH4_PLAY_MOVIE="700:0" SH4_DUMP=40 SH4_HEADLESS=1 timeout 40 ./sh4.exe "D:/Games/Silent Hill 4" >/dev/null 2>&1
last=$(ls frame_*.bmp | tail -1)
powershell -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$b=[System.Drawing.Bitmap]::FromFile((Resolve-Path '$last').Path); '$1 ($last): centre=' + \$b.GetPixel(512,384) + ' | 100,100=' + \$b.GetPixel(100,100) + ' | 900,700=' + \$b.GetPixel(900,700)"
