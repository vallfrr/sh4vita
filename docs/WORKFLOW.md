# Developer workflow (build, console, tests)

**Resuming work:** read [HANDOFF.md](HANDOFF.md) first. It identifies the active console owner, exact candidates, protected save and current next steps. Verify live processes before using the console; older session notes are historical.

Notes for people hacking on the port. Verified on Windows 11 + WSL Ubuntu with VitaSDK. End users do **not** need any of this: see the README.

## Rules
- Never commit game files (exe, data, demo, movie, sound), `port/gen/` or `re/out/` (code derived from the binary).
- `libshacccg.suprx` comes from your own Vita (`ur0:data/`), never from a download.
- No pirated distributions of the game: use your own legitimate GOG install.
- Always `quit all` (VitaCompanion) before pushing an eboot.
- Never combine `cd` and an `rm` with a relative glob.

## Recompiling the executable (`recomp/x86rec.py`)
`python recomp/x86rec.py` (~2 min) regenerates `port/gen/` (never versioned, ~68 MB of C) from `game/sh4.exe` plus the function analysis in `re/out/` (produced by `re/ghidra/ExportFunctions.java`, or the REA bridge, see `re/LEDGER.md`).
- `X87_STATIC=0` selects the old dynamic x87 stack (`c->st[ftop]`). The default **static x87 stack** gives every function that uses x87 with a consistent depth at all joins local `double` variables; functions with an inconsistent depth fall back to dynamic mode (report line "x87 dyn: merge mismatch"). A wrong estimate never breaks execution, it only costs speed.
- Validation: `recomp/difftest.py` (22,734 cases, 0 failures) and `recomp/difftest_x87.py 8 [--static]` (14 known pre-existing edge-case failures: fyl2x x10, fsqrt x4 on NaN/status edges). Both need SDL2 (x86rt.c includes SDL_mutex.h).
- `WRAPS` in `x86rec.py`: the recompiled body becomes `F_xxx_orig` and `F_xxx` calls a native function that may call it back (used for `nat_lightglow`). Unchanged files are not rewritten, so regeneration only recompiles the C files that changed.
- Options extension test (no game data): `gcc -std=gnu11 -Isrc/rt test/options_test.c src/rt/options.c -lm -o ../build/options_test.exe`. Run with a new disposable directory: `options_test.exe <directory>`, then `options_test.exe <directory> 6` to check persistence in a fresh process. Covers bounds, staging, Yes/No, whole-struct detection, row remapping and 5 labels. `camera.cfg` in the selected directory is modified; never point it at the user's game data.

## Vita build
```
cd /j/sh4vita && MSYS_NO_PATHCONV=1 wsl -e bash /mnt/j/sh4vita/port/build.sh -DSH4_TRACE_CALLS=OFF -DSH4_GUEST_MB=200 > build/vita_build.log 2>&1
grep -n "error:" build/vita_build.log
```
- `VITASDK` = `~/vitasdk` in WSL; vitaGL is built from source in `~/src/vitaGL`. Output: `eboot.bin` in the build dir and `build/sh4.vpk`.
- **Required depth retention**: build vitaGL with `make -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 -j16` (`-B` is needed when changing flags; make does not track them). `make NO_DEBUG=1 STORE_DEPTH_STENCIL=1 install` installs that build. An isolated archive can instead be selected with `-DSH4_VITAGL_LIBRARY=/absolute/path/libvitaGL.a`. On PowerShell, quote the complete `-D...=.a` argument or use `wsl -e bash -c 'bash ... -D...=.a'` to avoid `.a` being split off. The old SDK library is backed up in `build/vitagl-depth/libvitaGL-original.a`; it reproduces invisible actors in subway scene 5. The corrected archive is installed in the SDK, without source modifications to vitaGL.
- `build.sh` checks the selected archive for the depth force-load/store entry points before linking. The old configuration is rejected rather than silently producing an eboot with invisible actors. Diagnostics: `build/vitagl-depth/guard-old.log` (expected rejection), `guard-new.log` (successful build).
- **Isolated shared-depth build for the current candidate:** `port/patches/vitagl-shared-depth.patch` applies to vitaGL commit `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`. Use a separate checkout; leave `~/src/vitaGL` and the installed SDK library untouched. In WSL, from a fresh clone at that commit, run `git apply /mnt/j/sh4vita/port/patches/vitagl-shared-depth.patch`, then `PATH=$HOME/vitasdk/bin:$PATH make -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 -j8`. Build the port with `B=$HOME/build/sh4vita-shared bash /mnt/j/sh4vita/port/build.sh -DSH4_TRACE_CALLS=OFF -DSH4_GUEST_MB=200 -DSH4_VITAGL_LIBRARY=/absolute/path/to/the/isolated/libvitaGL.a`. The patch restores the original renderbuffer-format checks and shares depth/stencil storage across FBOs that attach the same logical renderbuffer. The console test candidate was built from this patch; compare its archived SHA and the targeted regression notes in `build/room-audit/` before replacing it.
- **Isolated compiled-cache backend:** `port/patches/vitagl-sh4-cache.patch` applies directly to the same vitaGL commit and includes the shared-depth fix. Apply only this combined patch in a separate checkout, then build with `make -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 HAVE_SHADER_CACHE=1 -j8`. Select its absolute `libvitaGL.a` using `-DSH4_VITAGL_LIBRARY=...` and pass `-DSH4_REQUIRE_SHADER_CACHE=ON -DSH4_SHADER_CACHE_DIR=/absolute/path/to/validated/cache` to `port/build.sh`; always retain `-DSH4_GUEST_MB=200`. The bundled cache is a VPK build input and remains outside Git. See [prepared data](PREPARED-DATA.md) for the supported GOG executable, cache validation and fallback behavior.
- **Experimental compressed-texture backend (29 October):** `port/patches/vitagl-sh4-dxt-blocks.patch` is a supplement to the compiled-cache patch above, on the same upstream revision. Apply it only in a separate checkout, then use the same full build flags. It replaces the power-of-two 2D UBC transfer with CPU swizzling in **4×4 block units**. The port conservatively keeps thin/non-power-of-two texture chains in software. This backend must be paired with the candidate port and remains opt-in pending qualification; do not enable `SH4_HWDXT` with an old library. The legacy option tests presence: even `SH4_HWDXT=0` enables it; omit the variable to disable it. No SDK installation is needed. Evidence and limitations are in [the quality report](QUALITY-2026-10-29.md).
- **Always pass `-DSH4_GUEST_MB=200`** (it is a CMake cache value; otherwise the guest memory cap falls back).
- `src/vita/sce-padding.ld` leaves an address gap before the constructor/data segment for module and import metadata added by `vita-elf-create`. Without it, some valid link layouts leave less room than the converter needs and fail with `segment 1 overlaps`. The script is a link dependency; do not remove it when reproducing a build.
- Profile build: `B=$HOME/build/sh4vita_prof bash port/build.sh -DSH4_TRACE_CALLS=OFF -DSH4_PROFILE=ON -DSH4_GUEST_MB=200`, then env `SH4_PROFILE=start,end` and `tools/resolve_prof.sh`. `SH4_PROFILE_TREE=1` prints the inclusive call tree (`tools/resolve_tree.py`).
- `-DSH4_GEN_FLAGS=-Os` is the default for the generated code (smaller I-cache footprint was measurably faster than `-O2`); `-DSH4_GEN_FLAGS=""` = `-O2`. `-DSH4_HOT=ON` (hot-function grouping) gave no gain.
- Memory order at start-up (`main()`): guest memory first (`alloc_guest`: free RAM - 32 MB for vitaGL - spare, capped by `SH4_GUEST_MB`), then `plat_gl_init`, SDL, `rt_init_mem`. Host newlib heap 24 MB.
- Copy the eboot to Windows from PowerShell: `wsl -e cp /home/<user>/build/sh4vita/eboot.bin /mnt/j/sh4vita/build/eboot.bin` (from Git Bash `wsl cp` mangles paths).

## PC build (functional testing; Vita performance cannot be measured on PC)
```
cd port && export PATH="/path/to/mingw64/bin:$PATH" && mingw32-make -f Makefile.pc -j16 SHELL=sh
cd build/pc && SH4_RES=0 SH4_HEADLESS=1 SH4_STATS=1 ./sh4.exe "<GOG install folder>"
```
The PC Makefile does not track headers: `touch src/dx/movie.c` after editing `pl_mpeg.h`. Call-count profile on PC: `mingw32-make -f Makefile.pcprof OUT=../build/pc_prof`, then `SH4_PROFILE=start,end ../pc_prof/sh4.exe ...`.

## Console (VitaShell FTP + VitaCompanion)
- FTP: `ftp://<vita-ip>:1337/ux0:/data/sh4/`. PowerShell helpers: `. tools\vita.ps1`, then `Vita-Cmd "quit all" 1500`, `Vita-PushEboot` (no argument = normal build; other eboot: `-Eboot <path>`), `Vita-Cmd "launch SILH00004"`, `Vita-Log <file>`.
- Log: `ux0:data/sh4/sh4log.txt` (delete before a run). Poll it from one command (`for (...) { Start-Sleep 20; curl.exe -s ... }`).
- `ux0:data/sh4/env.txt`: one `KEY=VALUE` per line, read at start-up (before vitaGL). Write it with `[IO.File]::WriteAllText` (Set-Content breaks line endings).
- Screenshots: `SH4_SHOT=N[,M]` -> `ux0:data/sh4/shot_N.tga` (960x544 BGRA, top-down); `tools/shot2png.ps1` converts.
- `tools/vrun.ps1 -EnvLines "A=1","B=2" -Shots 3500 -Wait 220 -Log build\x.txt`: quit all, write env.txt, clear the log, launch, wait for the last shot, fetch log + PNGs. It keeps one `sh4.sav.prev` copy.
- With `SH4_LIVE=1` or `SH4_CTL=1`, `ux0:data/sh4/ctl.txt` (re-read every 30 frames) holds per-frame-range diagnostic variants: `skip=hex,hex`, `skipn=N`, `skipall=1`, `cut=hex`, `nouni=1`, `gpufin=0|1`, `split=N`. The `shotnow` trigger also requires one of these settings. They are disabled during normal play to avoid periodic filesystem stalls.

## Scripted play-through (title -> room 302)
`SH4_INPUT="frame:DIK_hex:duration,..."` (DirectInput scancodes: 0x11 W, 0x1e A, 0x20 D, 0x1c Enter, 0x01 Esc):
```
800:01:3,1300:1c:3,1500:1c:3,1700:1c:3,3200:1c:3,3400:1c:3,3600:11:150,3900:20:60,4100:11:200,4500:1e:60,4600:11:300,5000:20:40,5100:11:300,5500:1e:40,5600:11:300
```
- Esc at frame 800 skips the opening movie (Enter does not). Title around frame 1100. Enter x3 = New Game -> difficulty -> intro text -> room. Frame numbers match PC (`SH4_VTIME=1`) and Vita.
- **Save file trap**: even an empty `sh4.sav` puts the title cursor on *Load*. Remove `save/sh4.sav*` before a scripted run.
- PC: use a mirror folder (`build/pc/game`: junctions to the install's `data demo movie sound`, a copy of the exe, a clean `save/`):
  `cd build/pc && SH4_VTIME=1 SH4_HEADLESS=1 SH4_RES=0 SH4_STATS=1 SH4_DUMP=100 SH4_INPUT="<route>" timeout N ./sh4.exe "<mirror>"`, then `tools/sheet.ps1 -from A -to B -cols 4` for a contact sheet of `frame_NNNN.bmp`.
- **PC regression (required after any rendering/runtime change)**: `bash tools/pc_regress.sh [sync|lazy|thread]` replays `tools/pc_reference_route.txt` + `SH4_WARP=3700:3` and compares available frames against `build/ref_head` (23 on 2026-10-24; `sync` is the reference; `thread` may differ by a few pixels). Diagnostic changes to `build/env.txt` do not change this route. Stop other PC test processes first: Windows cannot relink their running executable.

## World jump (`SH4_WARP`) to measure heavy scenes
`SH4_WARP=frame:type[,frame:type...]` (PC and Vita) calls the game's jump event (`0x4e6e00`, dummy object with the type at `+0x414`). Table `0x628110` (14 x 0x50 bytes): world, scene, position. Types: 0 hospital(7), 1/2 subway(2), 3/4 forest(3), 8 water prison(4), 9/10 building(5), 13 past(8) (5/6/11/12 = world 11, 7 = special). Worlds -> `o_*.bin` (table `0x62ff08`): 1 3ldk (apartment), 2 subway, 3 forest, 4 water, 5 build, 6 home, 7 hospit, 8 past, 9 last. World loader: state machine `0x4fa4b0`; `0x4f72b0(world, scene)` requests a change; `0xfd5a90` = current world.
For an exact room, optional `SH4_WARP_SCENE=N` (base 0) and `SH4_WARP_POS=x,y,z,rotation` temporarily override that table entry, then restore it after the game copies the values. Subway corridor with dogs = `SH4_WARP=3700:1 SH4_WARP_SCENE=5 SH4_WARP_POS=0,0,0,0` (`sb05.bin`). These are diagnostics; the rest of the story state is not restored by a warp.
Example (room reached around frame 3500): `SH4_WARP=3700:1 SH4_INPUT="800:01:3,1300:1c:3,1500:1c:3,1700:1c:3,3200:1c:3,3400:1c:3"`.

## Environment variables
| Variable | Effect |
|---|---|
| `SH4_STATS` | `[perf]`/`[q]`/`[stats]` lines (always on on Vita); `tools/perftab.py <log> <from> <to>` tabulates them |
| `SH4_SHOT`, `SH4_IPSHOT` | screenshots (swap / interpolation triplets) |
| `SH4_RES` | `0` = 640x480, `LxH` = custom (Vita forces 640x362) |
| `SH4_VBO`, `SH4_NOVBO`, `SH4_NOIBO` | vertex/index buffer caches (VBO on by default on Vita) |
| `SH4_PREP=sync|lazy|thread`, `SH4_PREP_KB` | D3D8 prep thread mode (default sync on PC, thread on Vita) |
| `SH4_NORTHREAD`, `SH4_QRING_KB`, `SH4_INTERP=0`, `SH4_SPLIT=N` | GL queue / interpolation / scene splitting |
| `SH4_HWDXT` / `SH4_SWDXT` | hardware vs CPU DXT decoding |
| `SH4_TEXMAX` | max texture size (default 256) |
| `SH4_TEX32` | `1`: experimental RGBA8 texture uploads, without 16-bit conversion; higher VRAM usage (off by default, see `TEXTURE-QUALITY.md`) |
| `SH4_INPUT`, `SH4_NOPAD`, `SH4_VTIME`, `SH4_HEADLESS`, `SH4_DUMP` | scripted input / virtual time / headless / frame dumps |
| `SH4_PLAY_MOVIE=frame:index` | start `m_<index>.pac` at the given frame (add a late harmless key in the route so Esc does not stop it) |
| `SH4_MOVIE_BENCH=secs[,delay,core,...]` | decode/convert/audio benchmark of m_00 |
| `SH4_TRACE_*`, `SH4_FSDEBUG`, `SH4_PH=1`, `SH4_QPROF=1`, `SH4_GPUFIN=1` | diagnostics (timers cost ~0.8 us per read on Vita) |
| `SH4_SPARE_MB` | user RAM kept free for threads (default 12) |
| `SH4_GLOW=0` | re-enable the light-glow grid on Vita |
| `SH4_HZ=n` | game loop frequency (test only; the game logic is per iteration, so it changes game speed) |
| `SH4_WIN=WxH`, `SH4_WAV=file` | PC borderless window / mixed audio dump (used by `tools/rec_pc.ps1` to capture gameplay video) |

Movie audio on PC: `gcc -O2 -Isrc test/plm_wav.c -o plm_wav` then `plm_wav m_00.pac out.wav 30` (stats + WAV).

## Reverse engineering
- Decompilation output: `re/out/decomp/0xADDR.c`; findings ledger: `re/LEDGER.md`; game map: `docs/GAME.md`.
- Ghidra headless export: `analyzeHeadless <proj> sh4 -import <exe> -scriptPath re/ghidra -postScript ExportFunctions.java <out.json> -deleteProject` (JDK 21, Ghidra 12.x).

## Remote play (driving the running game from the PC)
`. toolsita.ps1; . tools\live.ps1` (VITA_IP set), then:
- `Live-Tap "20" 560` = DirectInput key 0x20 held 560 ms (via `ux0:data/sh4/key.txt`, consumed by `dinput.c`; keys: 11 W, 1f S, 1e A, 20 D, 1c Enter, 01 Esc, 39 action), `Live-Keys "39" "<ctl tokens>"` = key held while present in `ctl.txt`.
- `Live-Shot "name"` = capture via `shotnow` (retries: a capture is often half-rendered), PNG in `build/live/`; `python tools/bright.py in.png out.png 4` brightens dark scenes; `tools/sheet.py` makes contact sheets; `tools/tga2png.py` converts `frz_*.tga`.
- `Live-Log`, `Live-Clean`; `tools/lab.ps1` (`Lab-Start`, `Lab-Window "<tokens>"`) reproduces the window interaction ("What's with this room?"): new game, spawn, 3x turn right, forward, action.
- ctl.txt tokens added for diagnostics: `gray=1` (clear/overlay/freeze logs; 2 = per-draw gray coverage, 3 = render-target switches), `texchk=1` (stale texture check), `rtlog=1` (per-draw painted pixels, with glFinish), `rtsync=1` (glFinish before sampling an RT texture), `nofrzsync=1` (disable the freeze sync), `tracenow=N`, `keys=hex,hex`.
- PC reference: same input script on the PC build (`SH4_RES=640x362`, `SH4_TGTLOG=1` logs render-target switches/clears, `SH4_RTMAX=1024` emulates the Vita RT limit).

## Room audit (development only)

`python tools/room_audit.py catalog --cases all --out build/room-audit/catalog` extracts the local, hash-checked GOG executable's room/door/background/preload tables. The manifest and JSON contain game-derived data: keep them in ignored `build/`, never commit them. See GAME.md for the distinction between rooms, resource-set revisits and forced background geometry.

After building with `SH4_GUEST_MB=200`, run:

```powershell
wsl -e cp /home/val/build/sh4vita/eboot.bin /mnt/j/sh4vita/build/room-audit/eboot.bin
python tools/room_runner.py vita --tour build/room-audit/catalog/tour.txt --out build/room-audit/vita-run --eboot build/room-audit/eboot.bin --group-world --chunk 40 --sweep --env SH4_AUDIT_HEAL=room
python tools/room_audit.py report build/room-audit/catalog/catalog.json build/room-audit/vita-run --out build/room-audit/vita-report.json --csv build/room-audit/vita-report.csv
python tools/room_gallery.py build/room-audit/vita-report.json build/room-audit/vita-gallery
python -m unittest discover -s tools -p test_room_audit.py
```

Use an actual compiled eboot path for `--eboot`; WSL's default output is `~/build/sh4vita/eboot.bin`. The output directory must be new. `pc` uses `build/pc/sh4.exe` and an isolated `build/pc/game` mirror; PC virtual-time measurements are not hardware performance measurements. The gallery tool requires Pillow, keeps its output under `build/`, and does not automatically mark images visually correct. CSV preserves each attempt separately, including its binary hash and diagnostic configuration flags.

Keep the matching SDK `sh4.velf` and `sh4` ELF next to each archived eboot as `<eboot-stem>.velf` / `.elf`, after checking the eboot matches that build. The Vita runner copies these optional symbol files into the run and records their hashes. A later SDK rebuild must not replace the symbols needed to analyse a previous binary's crash dump. Missing GL checks/timers are reported as unmeasured, not as zero errors/time.

The Vita runner reads the save twice before doing anything, records SHA256, stops the game before replacing the eboot, and restores only the original `sh4.sav` at the end, deleting `.bak/.new/.prev`. It restores the original environment and closes the application. Console availability must already be authorized. Creating a `STOP` file in the run's output directory requests a cooperative stop with restoration. If the host or network fails, inspect `original/backup.json` and restore that backup before normal play.

The opt-in `SH4_AUDIT=1` mode checks room identity and loading flags, waits for movies, takes two captures, records frame intervals, heap, render activity, GL errors and loading time, and checks draw ranges and shader-cache identity. The default arrival deadline is 1800 frames (`SH4_AUDIT_TIMEOUT`); a successful arrival gets its full settle/dwell interval even near that deadline. The runner monitors frame progress; screenshots are gzip-compressed on Vita and their CRC/decoded length is checked. Captures stall the game deliberately; capture intervals are excluded from steady-frame statistics. `--sweep` turns the camera without intentionally walking through doors. `SH4_AUDIT_HEAL=room` restores health at entry; `always` is reserved for stationary benchmarks. Neither applies in a normal run.

Evidence-complete means the requested scene completed with at least 30 measured frames, drawing activity and two valid capture files; a forced background must match the observed descriptor. It does **not** mean visually correct or fully playable. Images require inspection. A resource-set flag does not recreate all story state, and inferred origin spawns can put the camera outside the geometry. Keep timeouts and limited spawns explicit in the report.

PC `SH4_DUMP` and default audit BMPs read the raw game backbuffer, before the final gamma/scaling pass. Use `--env SH4_AUDIT_DISPLAY=1` for an audit capture of the final PC display, matching the stage of Vita TGA captures. Raw captures remain the existing non-regression reference; do not compare their brightness directly with a final Vita image. Match scene, entrance, story history, texture/RT limits and optional effects before attributing a visual difference to the platform. With CPU DXT decoding (`SH4_SWDXT=1` on PC), audit validation also reports any DXT1 texture whose decoded pixels use transparency, to check the Vita format assumptions.

For lower-overhead performance runs, add `--no-live --env SH4_AUDIT_LOG=0 --env SH4_AUDIT_VALIDATE=0`: this keeps the tour, captures and watchdog but disables per-frame audit logs and draw/cache/GL validation. Use `python tools/room_perf.py <batch-folder> --out <perf.json>` to extract existing `[perf]` windows between settled arrival and completion, excluding whole windows that overlap either capture. Such runs deliberately cannot satisfy the evidence-complete frame-log criterion or establish an absence of GL/buffer errors. Window means do not provide individual-frame percentiles. GL timers are unmeasured unless `SH4_PH` is enabled; render submission time is not GPU time.

`SH4_RAYTRI=0` selects the original ray/triangle routine. `SH4_RAYTRI_VERIFY=N` compares N real calls against the original, including output bits and x87 state; finish verification before timed measurements. `python tools/raytri_reference.py` extracts the reference into ignored `build/room-audit/` for `port/test/raytri_test.c`; never commit the extracted code.

`port/test/device_gamma_test.c` exercises the real gamma setter and render-consumer queue in sync/lazy/thread modes (no GL drawing). Compile this single translation unit with the normal SDL2 includes/libs and `-O2 -fwhole-program`; put `build/pc` on PATH for its SDL2.dll, then run each mode. Default gamma and raw reference images can remain unchanged even when this propagation is broken, so the setter/consumer test is required for this path.

`python port/test/shader_cache_test.py --cc J:/tools/mingw64/bin/gcc.exe` extracts and exercises the actual Vita shader-cache functions from `draw.c` with a simulated GL driver. It checks valid records, zero/overflowed/truncated lengths, failed-link source fallback with a fresh program, and invalid returned binary lengths. No game files or GL context are required. Hardware cache loading still needs its own checks.

For a normal-input diagnostic without a room tour, enable `SH4_AUDIT=1 SH4_AUDIT_PLAY=1`. This logs world, room, player position/rotation and nearby action candidates every 30 frames. Optional `SH4_AUDIT_SHOTS=4000,4050,4100` uses the same final-display capture path as room audits; it cannot be combined with `SH4_AUDIT_TOUR`. These traces and captures add overhead and must be disabled for performance comparisons. Debug source setup also accepts `SH4_WARP_WORLD=1..11` and `SH4_WARP_VISIT=0|1` alongside the existing scene/position overrides. A forced resource visit does not establish full story state. Always back up the original Vita save before a scripted launch.

Standalone shot requests target the upcoming present (`g_frame + 1`), because `g_frame` counts presents already issued when input is polled. Older builds could capture either that already-issued frame or the next one depending on consumer timing; compare their actual capture-frame tags before interpreting a pixel difference. The request remains owned by the consumer until its path and log metadata have been copied. `port/test/audit_capture_test.c` exercises the actual handoff, including 100,000 concurrent requests. With the PC SDL include/library paths configured, build it with `gcc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -Isrc/rt test/audit_capture_test.c -lSDL2 -o audit_capture_test` from `port/`.
