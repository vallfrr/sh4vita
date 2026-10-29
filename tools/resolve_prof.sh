#!/bin/bash
# Usage: resolve_prof.sh sh4log.txt -> "[prof] ... T +off" lines with the function name (build -DSH4_PROFILE=ON)
export PATH=$HOME/vitasdk/bin:$PATH
arm-vita-eabi-nm "${B:-$HOME/build/sh4vita}/sh4" | sort > /tmp/syms.txt
ref=$(grep ' F_00554100$' /tmp/syms.txt | head -1 | cut -d' ' -f1)
tr -d '\r' < "$1" | grep '^\[prof\].* T ' | while read -r _ pct n _ o; do
  a=$(printf '%08x' $((0x$ref + o)))
  nm=$(grep -i "^$a" /tmp/syms.txt | head -1 | cut -d" " -f3)
  echo "$pct $n ${nm:-?$a}"
done
