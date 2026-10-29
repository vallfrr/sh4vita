# Findings ledger — SH4 PC (GOG v5 80750)

Target: `game/sh4.exe` = `SILENT HILL 4.exe`, SHA-256 `CF7081A6C78DBE51F6DC2E1C9BD978D8486F1D7B59D7A3B88E027C9B39F21DD0`.
Ghidra/REA snapshot: `re/out/sh4.snapshot` (local, not versioned). REA is run through `re/rea.ps1 <command> ...`.

Source legend: **REA** = REA evidence (cite the ID) · **shell** = observation outside REA (tool named).
Status legend: **observed** · **inferred** · **unknown**.

## Porting questions

| # | Question | Status | Source / Evidence |
|---|----------|--------|-------------------|
| Q1 | Which Windows APIs does the game use? | observed | shell (`re/pe_imports.py`) + REA decomp |
| Q2 | How does the game read its files (`data/*.bin`, `*.pac`, `*.sh4`)? | partial | REA `ev_4d64a131…` (xrefs) |
| Q3 | Main loop, init, subsystem split | partial | REA `ev_b6ea0120…` (0x414a50) |
| Q4 | Exact Direct3D 8 interface usage (methods called) | partial | REA decomp + `re/d3d8_census.py` |
| Q5 | Video playback (MPEG-1): in-house decoder or DirectShow? | observed | REA `read_bytes` GUIDs (0x5d2514…0x5d2554) |
| Q6 | Audio: SDPA formats, DirectSound usage | partial | REA decomp 0x40c0f0 |
| Q7 | Input: DirectInput8, pad mapping | partial | REA decomp 0x407360 |

## REA session

- Initial analysis: `rea analyze` -> `ev_bbbefd12e6dc…`: image base 0x400000, 7664 procedures (7405 internal non-thunk), 1554 strings. Segments: `.text` 0x401000–0x5b7000, `.rdata` 0x5b7000–0x5d5000, `.data` 0x5d5000–0x13de000 (14 MB, mostly BSS), `.data1`, `.rsrc`.
- Persistent session: `node re/rea-bridge.mjs` (MCP stdio -> HTTP 127.0.0.1:47800), queries via `re/q.ps1 <tool> @{...}`. First call is about 136 s (Ghidra auto-analysis), later ones about 0.1 s.
- Full pseudo-code: `re/out/decomp/<address>.c` (7405 files, 0 failures), each file carries its `evidence_id`; index `re/out/decomp/index.tsv`. Procedure list: `ev_b69416a69deb…`.

## Observations

- **Q1 (shell, `pe_imports.py`)** — imports: KERNEL32 (97), USER32 (26), GDI32 `DeleteObject`, ADVAPI32 (registry), ole32 (`CoCreateInstance`…), `d3d8!Direct3DCreate8`, `DINPUT8!DirectInput8Create`, `DSOUND!#11`, WINMM (timers), IMM32. No Bink.
  - inferred: `DSOUND#11` = `DirectSoundCreate8` (usual ordinal, to be confirmed in code).
  - The shipped `d3d8.dll` is the "GOG.com DirectX 8 wrapper" 1.0.0.6361 (imports `d3d9!Direct3DCreate9`): the original game targets D3D8.
- **Data (shell, headers)**:
  - `movie/m_*.pac`: `00 00 01 BA 21` -> MPEG-1 system stream (inferred from the pack header).
  - `data/*.bin` (690 files): u32 count then an offset table (first = 0x80) — inferred, format to be confirmed via Q2.
  - `demo/*.sh4`: magic `SH4\0`; `demo/*.sdb`: unknown table.
  - `sound/*.pac`: magic `SDPA`.

- **Q3 (REA, 0x414a50 = WinMain)** — observed: window class/title "SILENT HILL 4:The Room", mutex "Silent Hill 4 Application", D3D init `FUN_00402f20` (failure -> MessageBox), about 60 init calls, `PeekMessage` loop + fixed step on `QueryPerformanceCounter` (`DAT_00610ddc` = period in ticks). Per frame: `device->BeginScene` (vtable +0x88), `FUN_0055ae20`, `FUN_00554390`, `FUN_004059e0`, `FUN_00553c70`, `FUN_0042d1a0`, `FUN_0056e470`, `FUN_004efbc0`, `FUN_00569230`, `FUN_0056e950`, then `FUN_0055abf0`. Global reload driven by `DAT_0107bf60` (states 1->2->3, fade `DAT_00ed5770`).
  - inferred: `FUN_0055abf0` = EndScene/Present (to be confirmed).
- **Q4 (REA, 0x402f20 = D3D init)** — observed: `Direct3DCreate8`, adapter enumeration, `CheckDeviceFormat` for `DXT1`/`DXT3`/`DXT5`, `GetDeviceCaps`, `CreateDevice(HAL, HARDWARE_VERTEXPROCESSING 0x40)`, back buffer `0x15` A8R8G8B8, depth `0x4b` D24S8, swap DISCARD; `DAT_005d5248 = 0x1e` (inferred: 30 fps). Global device = `DAT_006e7d94`.
  - Census (`d3d8_census.py`, direct calls on `DAT_006e7d94` only — calls through a local copy are not counted): `SetTextureStageState` 291, `GetRenderState` 34, `SetVertexShaderConstant` 34, `SetVertexShader` 22, `DrawIndexedPrimitive` 12, `DrawPrimitiveUP` 12, `Clear` 11, `SetPixelShaderConstant` 10, `SetRenderTarget` 9, `SetPixelShader` 9, `CreatePixelShader` 7, `DeleteVertexShader` 8, `Present` 4, `CopyRects` 3, `SetGammaRamp`, `CreateTexture`, `CreateImageSurface`, `Reset`, cursor… `SetRenderState`/`CreateVertexShader`/`SetStreamSource` are missing from the pattern -> called through a copied pointer (unknown, to be counted another way).
  - inferred: the game uses programmable D3D8 shaders (vs/ps 1.x) -> they have to be translated for vitaGL.
- **Q5 (REA, 0x413880)** — observed: `CoCreateInstance` of `CLSID_FilterGraph`/`IID_IGraphBuilder`, `CLSID_MPEG1Splitter`, `CLSID_CMpegVideoCodec`, `CLSID_CMpegAudioCodec`, `CLSID_DSoundRender` (+ `IID_IBaseFilter`). Cutscenes = DirectShow over MPEG-1. Vita replacement: pl_mpeg (video + audio, with a Layer I decoder added).
- **Q6 (REA, 0x40c0f0)** — observed: `DirectSoundCreate8` (confirms `DSOUND#11`).
- **Q7 (REA, 0x407360)** — observed: `DirectInput8Create`.
- **Q2 (REA)** — observed: getters `FUN_00572ba0` -> `".\data\"` (callers 0x414a50, 0x5696e0), `FUN_00572bc0` -> `".\sound\"` (callers 0x4092f0, 0x40c1c0, 0x56e840). Hard-coded archive names (`o_hospit.bin`, `o_subway.bin`, `sd_common.bin`…, 385 strings).

- **Q2 (shell, `re/bin_archive.py`)** — 689/690 `data/*.bin` match "u32 n + n increasing u32 offsets"; first offset 16-byte aligned (128: 533 files, 16: 95, 256: 33…). Only exception: `message_pasthome0_k.bin` (0 bytes). Parsing code not located yet (unknown) — not blocking with recompilation, the game code does the parsing.
- **Q2 (REA, 0x5684a0/0x568450)** — observed: cache of 3 handles for the `<dir>bgm.sh4`, `<dir>se.sh4` streams and a current file (`DAT_013cbcb0`); opened with `GENERIC_READ`, flags `0x60000000` (NO_BUFFERING|OVERLAPPED) unless `DAT_013cbd90`. Only 6 `CreateFileA` sites in the whole binary (0x406090 write, 0x407d60, 0x4092f0, 0x408ac0, 0x568450).

## Instruction set (REA `read_function_instructions`, `re/mnemonics.py`)

- 478,980 instructions, 230 mnemonics. Top: MOV, PUSH, CALL, ADD, POP, **FLD 17,835**, TEST, **FSTP 15,817**, CMP, LEA… Heavy x87 (FNSTSW 5,078 -> float comparisons through AH).
- SIMD present: MMX (MOVQ 1678, PUNPCK*), 3DNow! (PFMUL 813, PFADD, PFRCP, FEMMS…), SSE/SSE2 (MULPS, MULSD…).
- observed: `CPUID` in 7 functions; `0x596a10` (MMX/3DNow!/AMD ext. detection -> feature mask, D3DX style) and `0x57a451` (CRT, SSE2 bit 26 -> `DAT_013dd574`).
  - inferred: SIMD paths are selected through CPUID -> an emulated "x87 only" CPUID should avoid MMX/3DNow!/SSE (to be checked at run time: the recompiler emits a trap on any unhandled instruction).
- CRT functions recognised by Ghidra: 187 (SBH allocation, `__alldiv`/`__allmul`/`__aulldiv`, `__ftol`, `__chkstk`, `__SEH_prolog`, C++ EH `___CxxFrameHandler`/`__CxxThrowException@8`, `__setjmp3`/`_longjmp`, stdio…). CRT entry point: `entry` 0x5778cd. 116 internal thunks.

## Controls page extension (2026-10-24)

- **Observed**, existing decomp evidence `ev_c86254a693e768267b5cc404db623a1c4d1486518ae91a300e518f8a5db77df6` (`0x5254f0`): volume gauges are selected by text id 0x25/0x26, independently of row count/valtab. A custom label id alone renders a value string, not a gauge. The port wraps the row renderer and text lookup together.
- **Observed**, direct PE table read at `0x5c5af4` and initialization decomp `ev_670b3dfd64b3f4bf880820a8f9524e0e9f0e30a3eae854de1e9afed17d865ce0` (`0x524470`): hidden options defaults at +8/+0xb/+0x17 are 2/1/0.
- **Observed**, `ev_055b042234b95c0c43169de847c60f542daf8eb12a9fa7f9710219f2b8cbaea3` (`0x526810`): completed options acceptance returns 1; No sets `menu+0x114`; cancel returns -1. Console test: accept sensitivity 4, preview 5 then No, restart -> load 4. Evidence: `build/live/options-vita-save-log.txt`, `options-vita-no-log.txt`, `options-vita-reload-log.txt` and screenshots alongside them.
- **Observed**, local archive inspection of `message_common_eu.bin`, cross-checked against known Button Config/First Person Camera strings: European byte mode header 0x8000, terminator 0xffff, Latin glyphs = 0x100-ASCII, space 0, é/è/à/á = 0x17/0x18/0x20/0x1f. The port encodes its own text; no extracted strings or archive bytes are committed. French label/help visually checked on Vita; five labels checked by the native test.

## Invisible subway actors investigation (2026-10-24)

- **Observed**, live Vita session: `build/live/missing-person-still-nopost-17491.png` shows Henry and dogs with `skip=144`; restoring default yields `missing-person-still-default-19711.png` with actors absent. `skipn=5` also removes the final fullscreen quad but does not restore actors (`missing-person-still-noquads-24451.png`). Thus the final compositing quad alone is not the cause. The trace shows 3 FVF 0x144 draws into a 256x256 RT after drawing the environment and before the actor passes (`missing-person-trace.txt`). No restart was performed for these comparisons.
- **Observed**, existing decomp `ev_717adc5b331f9a837b8ed922673a40b6d00d2aac9748c5d01d69a8826c7b4ca6` (`0x443310`): procedural lighting draws into `DAT_00fce5a8` with the main depth surface, clears color only, then calls `0x403780` to restore the main color/depth targets. Other target-changing helpers: `0x403620/0x403680` (auxiliary textures), `0x4037a0` (scene textures with shared main depth), `0x55f660` (shadow/stencil pass). Callers include freeze/fade (`0x55a5c0`), dialogs (`0x50c640`), screen effects (`0x565d70/0x565f70/0x566b10/0x567a30`) and shadow dispatch (`0x560080`). Backend target changes and explicit queue flushes therefore need depth retention globally.
- **Observed**, pristine local vitaGL source `2bdbe89`, `gxm.c:init_depth_stencil_buffer`: forced depth load/store is compiled only with `STORE_DEPTH_STENCIL=1`. `arm-vita-eabi-nm -u` on `gxm.o` extracted from the installed archive confirms neither force-load nor force-store is imported; the candidate built with that flag imports both. Original archive SHA-256 `d36cae1f16a0000561ddf2dcdcebac7e75fc5da1ab325c7f50ab7a2bdbbd6821`, retained locally under `build/vitagl-depth/`.
- **Observed, controlled hardware A/B**: two eboots built from the same recompilation and application sources, differing only in vitaGL depth retention. `SH4_WARP=3700:1 SH4_WARP_SCENE=5 SH4_WARP_POS=0,0,0,0`, W at 3850 for 40 frames, screenshot at 4000, pause at 4100. Old build: actors absent (`build/live/depth-ab-old.png`); `STORE_DEPTH_STENCIL=1`: Henry/dogs visible (`depth-ab-new.png`). Logs: `build/vitagl-depth/ab-old-log.txt`, `ab-new-log.txt`. No draw-skipping token was used. This establishes loss of retained depth/mask across the lighting scene break as the cause of this incident; it does not claim every room has been replayed. The build now checks the selected library for depth force-load/store support.

## Known limits

- Windows REA/Ghidra = read-only P0 profile: no annotation/renaming in the database. Recovered names live here (Symbols table below). Option: run REA under Linux (WSL) if annotation becomes necessary.

## Symbols

| Address | Name | Status | Evidence |
|---------|------|--------|----------|

## Cutscenes (DirectShow) — observed via REA, implemented in port/src/dx/movie.c

- **0x413880 (ev_92eabe0e3fbd…)** — observed: the game creates through `CoCreateInstance`: FilterGraph (+ `IID_IGraphBuilder`), MPEG1Splitter, CMpegVideoCodec, and if `DAT_006018d8 != 0` CMpegAudioCodec + DSoundRender; its own render filter (0x180-byte object, DirectShow base classes compiled into the exe, 0x5af000–0x5b3e00). Assembly: `AddSourceFilter(file)` then `Connect` source->splitter, splitter["Audio"/"Video"]->codec->(DSoundRender / renderer). Interfaces queried on the graph: IMediaControl, IMediaEvent (b6), IMediaPosition (b2), IBasicAudio (b3).
- **0x413530 (direct disassembly, outside Ghidra)** — observed: `CheckMediaType` only accepts `FORMAT_VideoInfo` + `MEDIATYPE_Video` + **`MEDIASUBTYPE_RGB32`** (e436eb7e…); RGB24 is refused (`VFW_E_TYPE_NOT_ACCEPTED`, seen at run time). `SetMediaType` (0x413590) reads width = `biWidth`, height = `|biHeight|`.
- **0x412a50/0x412ca0/0x412ef0 (ev_0303938083…)** — observed: the movie is played into 2 A8R8G8B8 640x480 textures (CreateTexture) whose LockRect pointers the game keeps; the guest renderer writes each frame there, flips `renderer+0x174` and raises `renderer+0x178`; the game then calls `AddDirtyRect`. End of movie detected by `IMediaEvent::GetEvent` (code 1 = EC_COMPLETE, **0x4130b0**, ev_feaab393…).
- Movie streams: MPEG-1 system (pack header 00 00 01 BA), 640x480, 29.97 fps, 1 audio track (MPEG-1 Layer I, see `docs/GAME.md`); 14 files `movie/m_00..13.pac`. Host decoding: pl_mpeg.
- inferred: `DAT_006018d8` = "sound enabled" (not verified at run time).

## Memory (measured at run time on PC, SH4_TRACE_HEAP / SH4_TRACE_COPY)

- Guest heap in game: 106 MB stable (peak 170 MB after the fix); of which game arena 64 MB (VirtualAlloc @0x551fa4) and 23 MB for `sound.pac` (@0x408b8a).
- observed: 2 system surfaces of 4096x4096 (64 MB each) created at startup by 0x403250 and never read back -> GL->RAM readback made lazy.
