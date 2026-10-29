#!/bin/bash
# Usage: resolve_trace.sh sh4log.txt  -> names of the guest functions in the [calltrace] ring (build -DSH4_TRACE_CALLS=ON)
export PATH=$HOME/vitasdk/bin:$PATH
arm-vita-eabi-nm "$HOME/build/sh4vita/sh4" | sort > /tmp/syms.txt
ref=$(grep ' F_00554100$' /tmp/syms.txt | head -1 | cut -d' ' -f1)
tr -d '\r' < "$1" | grep '^   T' | awk '{print $2}' | while read o; do
  a=$(printf '%08x' $((0x$ref + o)))
  n=$(grep -i "^$a" /tmp/syms.txt | head -1 | cut -d" " -f3)
  echo "${n:-?$a}"
done
