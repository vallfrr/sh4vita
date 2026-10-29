#!/bin/bash
# Vita build from WSL: bash port/build.sh   (needs port/gen/: python recomp/x86rec.py)
set -euo pipefail
export VITASDK=${VITASDK:-$HOME/vitasdk}
export PATH=$VITASDK/bin:$HOME/tools/cmake/bin:$PATH
cd "$(dirname "$0")"
# ponytail: build outside /mnt/j, NTFS over 9p is slow for cmake
B=${B:-$HOME/build/sh4vita}
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release "$@"
# A GXM scene ends whenever the game switches render targets (lighting, shadows,
# blur, menus). Losing depth/mask state there makes later actor draws disappear.
# Refuse the old vitaGL configuration so a clean rebuild cannot reintroduce it.
vgl_lib=$(sed -n 's/^SH4_VITAGL_LIBRARY:[^=]*=//p' "$B/CMakeCache.txt")
case "$vgl_lib" in
    /*) ;;
    *) vgl_lib="$VITASDK/arm-vita-eabi/lib/lib${vgl_lib:-vitaGL}.a" ;;
esac
for mode in Load Store; do
    if ! "$VITASDK/bin/arm-vita-eabi-nm" -u "$vgl_lib" | grep -F "sceGxmDepthStencilSurfaceSetForce${mode}Mode" > /dev/null; then
        echo "vitaGL must preserve depth between scenes: rebuild it with make -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 install" >&2
        exit 1
    fi
done
if grep -q '^SH4_REQUIRE_SHADER_CACHE:BOOL=ON$' "$B/CMakeCache.txt"; then
    if ! "$VITASDK/bin/arm-vita-eabi-nm" -u "$vgl_lib" | grep -F 'sh4_shader_bundle_stage_root' > /dev/null; then
        echo "A release cache bundle requires the isolated vitaGL compiled-cache backend" >&2
        exit 1
    fi
fi
cmake --build "$B" -j"$(nproc)"
mkdir -p ../build && cp "$B/sh4.vpk" ../build/
ls -la ../build/sh4.vpk
