# Game map (SILENT HILL 4.exe, GOG v5) — addresses useful for the port

Complements `re/LEDGER.md`. Addresses are exe VAs (image base 0x400000). Status: observed unless noted.

## State machine
- State stack: level at `0x1236d48`, states at `0x1236d4c[level]`.
  `0x555c20` level++, `0x555c30` level--, `0x555c40` reads the state, `0x555c50(n)` writes it (clears deeper levels), `0x555c80` state++.
- Top mode `0x611964`, dispatch `0x42d1a0`: 0 = `0x42ccd0` init, 1 = `0x42cd40` logos (-> 3 if flag `0x13d1970` is set, getter `0x56f130`,
  set after the config is read, otherwise -> 2), 2 = `0x42cde0` opening movie (`0x5211b0`), 3 = `0x42ce40` (calls `0x52a760`, stores its return value).
- `0x527a50` writes the byte `0x65519c`.
- Main loop: WinMain `0x414a50`, fixed QPC step at 30 Hz (`DAT_00610ddc` period), frame rate `0x5d5248` = 30.

## Title screen (`0x52a760`, case 3)
- Idle counter `DAT_010928a4` = fps x 60 (1800); reset when |stick x/y| >= 0.5 (floats `0x1236c54`/`0x1236c58`),
  mouse (`0x5ff28c`/`0x5ff2bc`, `0x5ff290`/`0x5ff2c0`) or a button (`0x107be48`). At 0: `DAT_00655280 = 2`, returns 2 -> attract movie.

## Input (DirectInput8)
- Pad read `0x4074c0`: `GetDeviceState(0x110 = DIJOYSTATE2)`.
- `FUN_00406f50(axis, previous, id)` threshold +-300: lX -> id 0, lY -> id 4, lRy (off 0x10) -> id 8, lRx (off 0xc) -> id 0x14.
- POV: low word 0xFFFF = centred. Direction bits at +0x38 -> shorts (`FUN_004067c0`) -> normalised floats (`0x553d20`).
- EnumObjects callback `0x4065e0`: sets `DIPROP_RANGE` -1000..1000 per axis (DIPH_BYID, obj = dwType).
  **Without EnumObjects, axes range over 0..65535 -> centre 32768 > threshold -> the stick is "pushed" permanently**
  (this blocked the attract movie and would make the character drift). Fixed in `port/src/dx/dinput.c`.
- Camera bindings also use lZ (0x8) and lRz (0x14) through `input.ini`; the first threshold loop alone does not describe all bound axes. Third-person right-stick camera mapping is Z/Rz; first-person mapping uses X/Ry. See the player-mode notes below for worlds 6/11, which the earlier world-number heuristic misclassified.
- Buttons: `rgbButtons` (0x30..) read by a loop in 0x4074c0; POV `rgdwPOV[0]` (0x20), diagonals handled.
- `input.ini` (written by `0x406090`): KB_*/Pad_*; pad defaults: 0 action/confirm, 1 cancel/run, 2 items, 3 album, 4/5 side steps,
  6 rear camera, 7 aim, 8 item info, 9 pause, 10 dodge, 11 map. Esc = KB_SkipDemo (skips the opening movie).
- Save: single file `save\sh4.sav` (694,288 bytes: options + 10 slots). Even when empty, its presence puts the title cursor on *Load*.

## Movies (DirectShow)
- 14 x MPEG-PS: MPEG-1 video 640x480 29.97 fps (stream 0xE0) + **MPEG-1 Layer I** audio 48 kHz 192 kbps stereo (stream 0xC0,
  header `FF FF 64`) (`movie/m_00..13.pac`, 619 MB). The graph is built by the game; the host (`port/src/dx/movie.c`)
  provides the source/splitter/codecs (pl_mpeg + a home-made Layer I decoder); the render-to-texture filter is game code (guest Receive).
- Movie texture: 640x480 A8R8G8B8, one level, double buffered (`DAT_00ecb190/194`); detected by its size in `tex_gl`
  during playback (`g_movie_w/h`). The game requires 640x480 frames (half resolution breaks the layout).
- The main loop `0x412ca0` polls `0x413170` during a movie: `Sleep(2)` then retry, up to ~20 times,
  until Receive sets the "new frame" flag (+0x178 of the render object); the buffer index (+0x174)
  flips every frame. **The game's frame rate during a movie = the rate at which the host delivers frames.**

## Options menu (console version)
- Initial language: `0x413ed0` reads the emulated registry's `Install Language` for Windows error dialogs (`0xecb338`), not the in-game options. `0x524470` copies the 36-byte default template at `0x5c5af4` when no options save is loaded; language is byte `+0x11` (1=English, 2=French, 3=German, 4=Italian, 5=Spanish). After loading the executable, `rt_options_language_default` applies `SH4_LANG` to this template only. Existing saves and later menu choices retain priority. `port/test/language_test.py` exercises the actual generated initializer and menu setter with the user's supported executable.
- 3 pages x tables of 0x1c-byte rows in `.data`: `0x654cc0` (Video&Sound: Resolution, Brightness, Gamma, BGM, SE, Sound, Exit), `0x654d88` (Control), `0x654e50` (Others);
  row counts at `0x655034`/`0x655040`, titles at `0x65504c`, table pointers at `0x655058`. Rows: `{input, getter, animation, values, count, text_id, help_getter}`. `rt_keep_res` (x86rt.c) rewrites the tables on the first Present: page 1 without Resolution; page 2 original rows `0,1,5,2,6` with row 2's former Vibration handlers repurposed for sensitivity; page 3 without Realtime Shadow. Counts: 6/5/7.
  Hidden defaults from `0x5c5af4`: Head Motion (`options+8`) = 2, Vibration (`+0xb`) = 1, First Person Movement (`+0x17`) = 0. They are forced in both live and cancel copies. Walk/run (`+0x18`) and camera inversion (`+0x16`) remain adjustable.
  Volumes are adjustable. `options.c` wraps `0x523aa0` to remap row highlights, and `0x523d20` to compare the complete 0x24-byte options struct (`menu+0xb0` vs `+0xd4`) plus the private sensitivity. Menu = `0x1091a20`, live options = `0x1091ad0`.
- Sensitivity: wrappers on `0x524980` (input), `0x413c60` (getter), `0x525450` (help). Same input repeat/sound as volume input `0x524c00`, without changing audio. Values 0..10 (default 3) stored in `camera.cfg` in the data directory. `0x523e40` snapshots on entry; `0x526810` returning 1 with `menu+0x114 == 0` commits; No sets `+0x114` and discards on exit. An interrupted menu never commits. `dinput.c` scales the first-person right-stick X/Y and third-person Z/Rz, preserving movement and cursor response.
- Gauge renderer `0x5254f0` checks text ids 0x25/0x26, **not** the values/count fields. `nat_optrow` temporarily selects id 0x25 for the camera row; `nat_opttext` wraps `0x50c440` to return its own label instead. Synthetic ids: 0x7000 label, 0x7001 help. Stock BGM label is unchanged.
- Text lookup: `0x5239a0` -> `0x50b580(bank,id,x,y)` -> `0x50c440` -> `0x5588a0`. European strings in `message_common_eu.bin` start with `0x8000` (byte glyphs), end with `0xffff`. Latin glyphs = `0x100-ASCII`, space = 0, é = 0x17, è = 0x18, à = 0x20, á = 0x1f. `options.c` encodes original labels/help in EN/FR/DE/IT/ES into guest heap memory, without distributing game assets.
- Tested on PC (2026-10-23): every row of the 3 pages changes; languages EN/FR/DE/IT/ES (menus + title); "Yes" applies and writes `sh4.sav`.

## Rendering / D3D8
- D3D init `0x402f20`; global device `DAT_006e7d94`; resolution options `0x5d524c`/`0x5d5250` (w/h), indices `0x610dd4`/`0x610dd8`;
  options application `0x524320` (resets to 1024x768 -> `rt_apply_res` replayed in `dev_Reset`).
- Hottest guest functions (Vita profile): `F_00432f60`, `F_00559da0`, `F_00424830`, `F_00563d20`.

## Room census / diagnostic tour (2026-10-24)

- Door tables: `0x62fffc[world]`, header `{max_scene, door_count, connections, callbacks, room1, ...}`. Each room blob starts with a byte count; its records start at +4, stride `0x38`. Door id at +0xc, source room +0xd, spawn xyz +0x20, direction +0x2c. Getters `0x4fa070`, `0x4fa130`, `0x4fa160`; `0x4f0620` resolves the entrance and calls `0x52cee0` to position the player. Door tables alone cover 207 entries and omit special rooms.
- Background table: `0x62fdb0[world]`, stride `0x1c`, resource id +0x18. Resource names: `0x69f698 + (id & 0xfff)*12`. Preload arrays: `0x62d514[world][scene]` / `0x62d548[world][scene]`, lists ending with `0x0fffffff`; `0x4fc830` selects the second set from `0x13d812d & 4`. `0x4f3510` loads the room list. This is a resource-set switch, not a complete story checkpoint.
- Actual worlds 9/10/11 = final arena / tunnels / spiral stairs. `st*.bin` belongs to **world 11**, not apartment 302. `fr50`, `fr51`, `fr52` are added resources in forest rooms 10/14 (first/revisit lists), not additional scene ids 50..52. Scene 8:22 has an empty preload list, no doors and aliases cm21: reserved, excluded from automatic tests.
- Apartment world 1 has 7 logical rooms and 30 background descriptors (`rl00a` etc.). `0x4f6eb0` selects a descriptor using the flag/selector pairs at `0x62fdf0`, `0x62fe40`, `0x62fe68`, `0x62fea0`, `0x62feb8`, `0x62fec8`, `0x62fee0`; each list ends at a zero condition flag. Descriptor 28 (`rl02h`) is not selected by these lists. Tunnel scene 1 selects gt12 until flag `0x13d8181 & 2`, then gt01.
- `tools/room_audit.py` extracts the inventory locally from the checksum-verified executable; output belongs under ignored `build/`, never in Git. Current inventory: 229 logical entries, including the reserved alias; 228 eligible entries and 386 base/resource-set/background test cases (including the unused rl02h descriptor as an explicitly inferred geometry test). A warp does not reproduce all progression-dependent actors, events or interactions.

### Normal tunnel entry (2026-10-25)

Direct origin spawns in curved tunnels can show their outer mesh against black instead of a gameplay view. The normal hole interaction initializes an automatic camera path; sampling those origin spawns does not validate the path. `0x4d1920` maps source worlds 2..5 to tunnel scenes 1..4 (first resource set) or 7..10 (revisit), world 6 to 5, world 7 to 6 and world 8 to 11. Spiral scenes 1..5 map to tunnels 6..10. Tunnel scene 1 uses gt12 until `0x13d8181 & 2`; the original first-departure function `0x4e7b60` sets this flag.

`0x4d1b60` requests the tunnel world, records the source via `0x4d1450`, fills `0xfd1a6c/70/74` and advances the fade/event path. Calling just the world switch omits this context. Source-hole exit poses/condition flags are in 26 records at `0x618580`, stride `0x30`: world +0, scene int16 +4, flag int16 +6, xyz +0x10 and yaw +0x20. These are exit poses, not guaranteed interaction locations. A PC setup in 5:8 at (137,0,550), yaw 0 followed by normal Action/Yes input enters gt04, shows its textured inside camera and returns to the apartment. Other normal-entry paths and Vita replay are being checked; source setups and resource flags still do not recreate full story checkpoints.

### Intentional room effects versus suspected rendering faults

The original initializer `0x4f04e0` calls `0x530760` specifically in Building room 24 on revisit when `0x13d8116 & 8` is clear. This sets `0x10a19dc`; `0x52f260` then reverses the movement directions. The [authorised strategy guide, p. 101](https://www.silenthillmemories.net/publications/guides/silent_hill_4_the_official_strategy_guide_piggyback.pdf) describes a deliberately upside-down room during this visit. An inverted 5:24 capture is therefore a comparison reference, not sufficient evidence for changing the renderer. Static screenshots and resource-set switches need context before treating a strange scene as a port defect.

### Collision BSP lifetime

`0x561a20` installs the current collision resource. A valid header (`int16 == -0xef`) sets owner `0x12f6d70`, polygon pointer `0x12f6d74`, and optional BSP root `0x12f6d78`. The original null/invalid branch clears the first two but leaves the BSP root unchanged. Entering world 6 scene 21 directly from the apartment reproduces a use-after-free: `0x5346d0 -> 0x561cd0 -> 0x41dc50 -> 0x41da00` traverses the old tree, then treats reused position floats as child offsets and crashes.

`nat_collision_set` wraps the installer and clears the root whenever the original has rejected/cleared its owner. Valid resources keep the original path. This protects every query that shares the root (`0x561cb0`, `0x561cd0`, `0x561e20`, `0x561f90`), rather than special-casing a room or suppressing a collision query. `SH4_LEGACY_BSP=1` retains the original behavior for controlled reproduction. PC reproduction, native lifetime test and 23 reference captures pass. On Vita, the same 6:21 transition clears the stale root and completes the diagnostic interval with two captures (`build/room-audit/vita-bsp-pad`).

### Player control mode

`0x4fa3c0` sets `0xfd5ba4` to 0 for worlds 1 (apartment) and 10 (tunnels), and 1 otherwise. Movement paths `0x52d710` / `0x534620` use this flag. DirectInput now reads the actual flag instead of guessing from world numbers; world 6 (home) and 11 (spiral stairs) require third-person axes. Injected Vita input in 6:21 confirms right X -> Rz, right Y -> Z, left X -> X and neutral -> zero. This checks axis mapping, not physical stick feel.

### Ray/triangle hot path

`0x573a20` is the scalar ray/triangle intersection called repeatedly by enemy collision traversal (`0x492c00 -> 0x4937a0 -> 0x4932b0 -> 0x561bc0 -> 0x41d7b0 -> 0x41e6c0`). The native wrapper preserves the original double operation order, float spills, output write order and x87 comparison status. Modified math dispatch/constants, unusual stack aliasing, alignment, nonfinite and extreme inputs fall back to the original. `SH4_RAYTRI_VERIFY=N` compares real calls bitwise before timed runs; `SH4_RAYTRI=0` selects the original for A/B. Native differential testing covers one million cases; PC gameplay compared 100,000 calls and Vita 10,000 calls without a mismatch. Absolute performance still requires hardware measurement, with diagnostics overhead identified.
