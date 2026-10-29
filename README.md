# Silent Hill 4: The Room for PS Vita

Port of the latest GOG PC version of Silent Hill 4: The Room to the PS Vita.

No game files are in this repo or in the VPK. You need your own copy of the game.

## What you need

- A hacked PS Vita (HENkaku / Enso), at least 4 GB free on `ux0:`
- [Silent Hill 4 from **GOG**, version "1.0 gog v5"](https://www.gog.com/fr/game/silent_hill_4_the_room). Check: `SILENT HILL 4.exe` has SHA-256 `CF7081A6C78DBE51F6DC2E1C9BD978D8486F1D7B59D7A3B88E027C9B39F21DD0`. No other version works (no PS2 files either).
- `ur0:data/libshacccg.suprx` from your own Vita (ShaRKF00D).
- `sh4vita.vpk` and the `sh4prep` download for your computer from the [Releases](../../releases) page

## Install

Download the VPK and the preparer for your computer:

| Download | Platform |
|---|---|
| `sh4vita.vpk` | PS Vita |
| `sh4prep-windows.exe` | Windows |
| `sh4prep-linux-x86_64.tar.gz` | Linux x86-64 |
| `sh4prep-macos-arm64.zip` | macOS |

### Windows

1. Download and install the supported [**Windows GOG v5** version of Silent Hill 4](https://www.gog.com/fr/game/silent_hill_4_the_room) on your PC, using GOG Galaxy or the offline installer from your GOG library.
2. Run `sh4prep-windows.exe`. Select the game's **installation folder**: the folder containing `SILENT HILL 4.exe` alongside `data/`, `demo/`, `movie/` and `sound/`.
3. Choose a separate output folder and your language, then press **Prepare the files**.
4. When preparation finishes, continue with [Copy to the Vita](#copy-to-the-vita) below.

### Linux and macOS

Use the **Windows GOG v5** game files on these systems too. [InnoExtract](https://github.com/dscharrer/innoextract) can extract supported GOG installers without running them. If you already have the complete installed or extracted game folder, skip to step 4.

1. Download the **offline Windows installer** from your GOG library, including every accompanying `.bin` part. Keep the `.exe` and all its parts together in one folder, with their original filenames.
2. Install InnoExtract. On Ubuntu/Debian:

   ```sh
   sudo apt update
   sudo apt install innoextract
   ```

   On macOS, with [Homebrew](https://brew.sh/) installed:

   ```sh
   brew install innoextract
   ```

   For other Linux distributions, use your package manager or the [InnoExtract installation instructions](https://constexpr.org/innoextract/install).
3. Open a terminal in the folder containing the GOG installer and extract it:

   ```sh
   innoextract --gog --output-dir sh4-extracted "setup_silent_hill_4_the_room_1.0_gog_v5_(80750).exe"
   ```

   The filename may differ; replace it with the name of your downloaded installer. Keep the quotes around the filename, especially if it contains parentheses. Some GOG installers require an additional RAR extractor: if InnoExtract reports one missing, install `unar` or `unrar` through your package manager and retry. See the [InnoExtract manual](https://constexpr.org/innoextract/innoextract.1).
4. Extract the `sh4prep` archive for your system. Open a terminal in the extracted preparer folder and run:

   ```sh
   chmod +x sh4prep
   ./sh4prep
   ```

5. In the preparer, select the folder containing `SILENT HILL 4.exe` alongside `data/`, `demo/`, `movie/` and `sound/`. After InnoExtract, this may be `sh4-extracted/app/`; use whichever folder directly contains these files and directories.
6. Choose a separate output folder and your language, then press **Prepare the files**. Continue with [Copy to the Vita](#copy-to-the-vita).

The preparer rejects unsupported game executables (wrong versions or PS2 files).

### Copy to the Vita

1. Install `sh4vita.vpk` with VitaShell.
2. Copy the generated **`sh4` folder** from your chosen output folder to **`ux0:data/`**. USB is much faster than FTP. The result must look like this:

   ```text
   ux0:data/sh4/
     SILENT HILL 4.exe
     data/
     demo/
     movie/
     sound/
     sh4vita.json
   ```

3. Start Silent Hill 4 from the LiveArea.

No old save, development configuration or separately downloaded cache is required. The VPK supplies its compiled shader cache and reads it on demand. A missing or rejected cache entry is compiled by the Vita and saved locally. `libshacccg.suprx` remains a requirement; no full shader preparation is run at first launch.

Saves go in `ux0:data/sh4/save/`. The current log is `ux0:data/sh4/sh4log.txt`; the 19 previous launches are kept as `sh4log.txt.01` through `sh4log.txt.19` (newest first).

### Updating — WIP

Close the game and install the new VPK over the existing application with VitaShell. Keep `ux0:data/sh4/`, including your saves and settings. Normal port updates replace the application and its bundled cache; you do not need to prepare the GOG files again unless an update explicitly requires it.

## Controls — WIP

Cross action, Circle run/cancel, Triangle items, Square map, Select album, L rear camera, R aim,
Start pause. Left stick moves and side-steps, right stick turns and looks. Item info and dodge are not mapped yet (Vita has no L3/R3).

Options → Controls includes camera sensitivity (0–10, default 3) for the right stick in first and third person. Answer Yes when leaving options to save it (`camera.cfg` in the game data folder); No cancels the change.

## Progress — WIP

Working: menus, intro, new game, all 9 worlds, cutscenes with sound, audio, options menu (5 languages).
The game targets 30 Hz logic with interpolated rendering toward 60 fps. Some measured scenes stay near 30 logical fps, while enemy-heavy subway, water-prison and late-game scenes remain below that target. See the measured intervals and test limits in [the validation report](docs/VALIDATION-2026-10-25.md).
Movies run at about 30 fps. A 19,500 frame test with 16 world jumps did not crash.

Not tested on a real Vita yet: saving at a save point, Button Config, sleep/wake. Physical stick controls and the options menu have been checked by a player.

Known problems: some scenes are CPU-limited, first-time room loading can still pause for about a second, RAM is tight, and the light glow effect is disabled for speed. The development VPK can bundle compiled GPU programs and load them on demand; missing cache entries are compiled by the Vita and saved locally. See [prepared data](docs/PREPARED-DATA.md) for the tested scope and update behavior.
More numbers in [docs/STATUS.md](docs/STATUS.md).

## Problems — WIP

- "GAME FILES NOT FOUND": the exe and folders must be directly in `ux0:data/sh4/`, not `sh4/sh4/`.
- "SHADER COMPILER MISSING": put `libshacccg.suprx` in `ur0:data/`.
- sh4prep rejects the exe: wrong game version, you need GOG v5.
- Crash: open an issue with the log from the affected session. After a relaunch, it is normally `sh4log.txt.01`.

## Build from source

For development or resuming an existing session, start with [the current handoff](docs/HANDOFF.md), then [project status](docs/STATUS.md) and [the developer workflow](docs/WORKFLOW.md).

Not needed to play. You need the GOG exe as `game/sh4.exe`, Python 3 with Capstone, Ghidra 12 with JDK 21,
and WSL/Linux with [VitaSDK](https://vitasdk.org) and [vitaGL](https://github.com/Rinnegatamante/vitaGL) built with `NO_DEBUG=1 STORE_DEPTH_STENCIL=1` (use `make -B` when changing build flags).

```
analyzeHeadless <proj-dir> sh4 -import game/sh4.exe -scriptPath re/ghidra -postScript ExportFunctions.java re/out/functions.json -deleteProject
python recomp/x86rec.py                                    # writes port/gen/ (about 68 MB of C, not committed)
bash port/build.sh -DSH4_TRACE_CALLS=OFF -DSH4_GUEST_MB=200   # produces build/sh4.vpk
```

`sh4prep` is Rust: `cd tools/sh4prep && cargo build --release`.
There is also a Windows build of the runtime for testing (`port/Makefile.pc`).
Build options, env vars and test scripts: [docs/WORKFLOW.md](docs/WORKFLOW.md). Game internals: [docs/GAME.md](docs/GAME.md).

Layout: `recomp/` recompiler, `port/src/` runtime (Win32, D3D8, DirectX replacements, Vita layer), `tools/sh4prep/` data tool, `re/` reverse engineering notes.

## Legal

Silent Hill 4 belongs to Konami. This project is not affiliated with Konami, GOG or Sony.
The VPK is built from an exe you own; do not ask for game files here.

## Credits

vitaGL (Rinnegatamante), VitaSDK, pl_mpeg (Dominic Szablewski, with an added Layer I audio decoder), TLSF, SDL2, Ghidra, Capstone, ShaRKF00D.
