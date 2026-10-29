# Prepared data and VPK updates (experimental)

## Intended release layout

The supported GOG executable is fixed at SHA-256 `cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0`. Players prepare their own original installation with `sh4prep` once and copy the resulting `sh4/` directory to `ux0:data/sh4`. The original executable and game resources remain there, separately from the installed application.

Select the complete installed GOG folder, containing the executable and `data/`, `demo/`, `movie/`, `sound/`. Neither the executable alone nor the GOG installer is sufficient input. Normal preparation copies only those game files and writes `sh4vita.json`; it does not copy a PC save, `env.txt`, scripted inputs or developer caches, and no longer generates the old optional `shaders.bin`. A supported homebrew-enabled Vita with `libshacccg.suprx` is still required. No development SDK, FTP service, test plugin or prior save is needed to play.

The VPK supplies the port executable and a versioned **compiled GPU program cache**, read on demand from `app0:/shadercache/r1`. It does not contain the original GOG files, textures, movies, sound or save. Updating the VPK under the same `SILH00004` title ID can therefore replace the code and compiled cache without preparing the original game again. `save/sh4.sav` and `camera.cfg` remain in the persistent data directory; cache handling never replaces or deletes them.

The normal startup does not prewarm the entire bank. It first tries an immutable complete program from the VPK, then its versioned writable cache. The experimental backend can also reuse separately compiled stages supplied by the VPK. Missing or rejected entries retain the normal runtime fallback. An incomplete cache can still cause an unexpected first-use compilation, so coverage must be measured rather than inferred from the presence of a bundle.

The initial local export contains 256 complete program pairs and 244 compiled stages. These generated build inputs remain ignored by Git. The real v56 VPK update on the test Vita preserved `sh4.sav` byte for byte, along with the camera settings and original GOG executable. Its fresh-cache startup exposed vitaGL's eager creation of 512 bucket directories; v57 creates buckets only when writing them. With source compilation forbidden, no prewarm and old writable stages hidden, v57 rendered all 386 distinct catalog cases after six isolated retries: 772 valid captures reviewed, 1,409 VPK program hits and no unexpected alert. This sampled coverage does not prove a complete story playthrough or steady 30 fps. With the VPK cache and old stages temporarily hidden, a normal first boot regenerated seven writable programs from eleven source compilations; the next boot reused them without compiling. The save hash stayed unchanged. In a paired water 4:11 test with 596 measured frames each, guard27 averaged 46.859 ms and v57 averaged 47.005 ms, with byte-identical captures. This single instrumented comparison finds no large steady regression; it does not establish 30 fps or a clean-play benchmark.

## Building the compiled cache bundle

The developer generates and validates GPU programs on a Vita once, exports only those compiled records, and packages them with the matching renderer. Players do not perform that compilation. No console identifier or host pointer is serialized: matrix metadata uses program parameter indices, reconstructed on loading. Compatibility across the intended hardware and software configurations still needs qualification.

```text
python tools/shader_cache_bundle.py prepare build/room-audit/compiled-cache-export build/shader-cache
python tools/shader_cache_bundle.py validate build/shader-cache
```

`SH4_SHADER_CACHE_DIR` selects this directory during the Vita build. A release build must pass `-DSH4_REQUIRE_SHADER_CACHE=ON`; otherwise the build fails when the validated bundle is absent. CMake checks its executable hash, renderer ABI, compiler settings, allowed paths and file hashes and packages only the listed GPU records. The ignored data is included as compiled program resources in the VPK, never as source files in Git.

Complete-program records have a 24-byte envelope: eight bytes `SH4GXP1\0`, little-endian 32-bit version 1, 32-bit payload length and a 64-bit FNV-1a payload checksum. The payload retains the vertex/fragment length fields used by vitaGL. The loader bounds and checks records before driver calls and retries the writable cache if a bundled entry is invalid or fails to link. The ABI directory must change when serialization compatibility changes; shader source changes also change lookup keys.

For development, `SH4_SHADER_CACHE_ONLY=1` stops before any of vitaGL's three source-compiler entry points when a cache miss occurs. Combined with an inaccessible old writable stage cache and a fresh program-cache namespace, this tests whether the supplied cache is sufficient. `SH4_BUNDLED_CACHE=0` disables bundle reads for comparison. Neither flag belongs in the normal player configuration.

## Earlier bank-preparation prototype

Earlier `sh4prep` development builds also generated `shaders.bin` from the supported GOG executable. It contains the 210 static vertex shaders translated to GLSL, together with their original declaration/function addresses, color ordering and constant counts. Normal player preparation no longer creates this unused prototype file. Developers can still generate it through the explicit `SH4PREP_TEST_BANK` test workflow below. The C translator is shared with the game runtime; this raw bank is never included in Git or the VPK, and the compiled GPU cache described above is a separate build resource.

The PC tool does not run the Vita's shader compiler directly. The optional developer prototype `SH4_PREWARM=1` reads the prepared bank and compiles vertex stages on that console. It helped produce and diagnose the cache, but is not the intended release installation path and remains disabled by default.

The preparation uses temporary programs with a dummy fragment shader. These programs are never substituted for actual game programs. Only a vertex stage whose **exact translated Cg source and compiler options** match may be reused by the backend. The current pair's varying bindings are retained when loading a stage. Actual fragment stages and fixed-pipeline variants can still compile on first use, so preparing this bank alone does not establish that all gameplay stalls are gone.

During preparation the Vita displays a native `sceMsgDialog` system wait dialog. `SH4_PREWARM_UI=font` selects the diagnostic progress bar, and `SH4_PREWARM_UI=0` hides the interface. A successful readiness manifest avoids both preparation and its dialog on later launches; every referenced cache file must still validate. Missing files, corrupt payloads or incompatible compilation settings invalidate readiness and permit rebuilding.

The experimental backend also owns a list of allocations made by the runtime compiler. After copying GXP programs and reflection data into separate vitaGL allocations, it shuts down the compiler and then frees the compiler's remaining blocks. This limits retained scratch memory between programs and avoids the texture/video allocation failures observed during the initial full-bank experiment. It is isolated from the installed SDK and the qualified depth library until hardware non-regression checks finish.

The isolated Vita backend can be reproduced by applying [the combined patch](../port/patches/vitagl-sh4-cache.patch) to vitaGL commit `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`. It contains the shared-depth fix, exact compiled-stage cache, source-compilation guard and compiler scratch cleanup used by candidate v57. Build that separate checkout with `make -B NO_DEBUG=1 STORE_DEPTH_STENCIL=1 HAVE_SHADER_CACHE=1`; pass its `libvitaGL.a` through `-DSH4_VITAGL_LIBRARY=...` and require the validated bundle with `-DSH4_REQUIRE_SHADER_CACHE=ON`. The patch was checked against the clean commit and against the working candidate; neither the standard vitaGL checkout nor the installed SDK archive is modified by this procedure.

## Binary format

All integers are little-endian. The 64-byte header contains:

| Offset | Field |
|---:|---|
| 0 | Eight bytes `SH4VSB1\0` |
| 8 | Version `0x00010001` (format 1, translator revision 1) |
| 12 | Record count, 210 for this executable |
| 16 | Payload length |
| 20 | Reserved, zero |
| 24 | SHA-256 of the supported executable, 32 bytes |
| 56 | FNV-1a 64-bit checksum of the complete payload |

Each record contains six 32-bit values: function address, declaration address, BGRA mask, original stride, constant count, source length including its terminator. The NUL-terminated GLSL source follows, padded with zero bytes to a four-byte boundary. Records are sorted by function address. The loader bounds the file at 8 MiB and each source at 128 KiB, checks all record boundaries, addresses, counts, padding and terminators, and rejects the entire file on an error before compiling anything. The checksum detects accidental corruption; it is not a signature.

Changing the shared translator requires updating the bank revision in both the generator and loader. Changing backend translation, serialization or compilation settings also requires cache invalidation. Deleting the cache or deploying an incompatible update can require preparation again.

## Verification

`SH4PREP_TEST_EXE` points the Rust tests at a local, legally installed supported executable. `SH4PREP_TEST_BANK` optionally records the generated bank under `build/` for the portable loader tests:

```text
cargo test --release --manifest-path tools/sh4prep/Cargo.toml
python port/test/shader_bank_test.py
```

The runtime loader test builds the actual C loader and exercises valid, truncated, corrupt and structurally invalid records. `tools/pc_regress.sh` uses the checked-in `tools/pc_reference_route.txt`; changing a separate diagnostic `build/env.txt` no longer changes its route. Console tests must separate first preparation, later cache loading, first room visits, logical gameplay cadence and interpolation. A pregame preparation is deliberate work: use `room_runner.py --startup-seconds 600` when measuring it, while retaining a shorter gameplay stall limit.

## Optional transfer inventory (preparer 0.3 candidate)

New preparations also write `files.idx` and add `"file_index": "files.idx"`
to the existing format-1 JSON. The inventory lists only the copied original
executable and game resources. It contains relative paths and byte sizes, with
an entry count, total size and FNV-1a checksum of the inventory payload. It does
not contain saves, private settings, caches or game contents. The checksum is
an inventory consistency check, not authentication or a checksum of each asset.

The Vita candidate checks declared inventories before allocating guest memory.
Missing, truncated or wrong-sized files produce a startup error instructing the
player to copy the prepared files again; it never deletes or repairs saves.
Directories are enumerated in groups and their temporary metadata cache is
freed before the game starts. No periodic gameplay scan is added. Same-sized
content corruption is outside this check.

Older format-1 data without the optional field remain supported. An unreferenced
inventory is ignored, so VPK updates do not require re-preparation. Older VPKs
also ignore the new field and continue to use the same original game files.
Windows candidate preparation has been exercised on the complete supported
installation. Linux/macOS builds must be regenerated and qualified before this
preparer candidate is distributed.
