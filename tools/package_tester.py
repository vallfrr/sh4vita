"""Build a small private tester ZIP from a validated VPK and prep binaries."""

import argparse
import hashlib
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "build/release/github-assets"
PREPARERS = (
    "sh4prep-windows.exe",
    "sh4prep-linux-x86_64.tar.gz",
    "sh4prep-macos-arm64.zip",
)

README = """SH4VITA - EXPERIMENTAL TEST BUILD

Requires a modded PS Vita with libshacccg.suprx and your own PC GOG
Silent Hill 4: The Room 1.0 gog v5 (80750) game files.

1. On Windows, install the GOG game. On Linux/macOS, extract the GOG offline
   installer with InnoExtract, keeping its accompanying .bin files together.
2. Run sh4prep for your OS. Choose a language, the installed/extracted game
   folder and an output folder. The game folder must contain SILENT HILL 4.exe,
   data/, demo/, movie/ and sound/.
3. Install sh4vita.vpk with VitaShell. Copy the resulting sh4/ folder to
   ux0:data/ on the Vita, so the files are in ux0:data/sh4/.
4. Launch the game. Existing testers can install only the new VPK and keep
   ux0:data/sh4/, including their saves.

This build enables the experimental CPU, model, collision, audio, streaming
and interpolation optimizations. They have not been qualified together across
the complete game. If something fails, save ux0:data/sh4/sh4log.txt BEFORE
restarting, and send the room, steps and a screenshot/video. For a crash,
also send the latest ux0:data/psp2core-*.psp2dmp. Send reports to vallfrr.
The earlier intermittent texture corruption and elevator memory failure are
not claimed as fixed. No original game files are included in this ZIP.
"""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vpk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with zipfile.ZipFile(args.vpk) as vpk:
        names = vpk.namelist()
        cache = [n for n in names if n.startswith("shadercache/r1/") and n != "shadercache/r1/manifest.json"]
        if len(cache) != 500 or "shadercache/r1/manifest.json" not in names or "eboot.bin" not in names:
            raise SystemExit("VPK must contain an eboot and the validated 500-record GPU cache")
        if vpk.testzip() is not None:
            raise SystemExit("VPK contains a corrupt entry")
    payload = {"sh4vita.vpk": args.vpk.read_bytes(), "README.txt": README.encode("utf-8")}
    for name in PREPARERS:
        payload[name] = (ASSETS / name).read_bytes()
    expected = {}
    for line in (ASSETS / "SHA256SUMS.txt").read_text().splitlines():
        sha, name = line.split(maxsplit=1)
        expected[name] = sha
    for name in PREPARERS:
        if digest(payload[name]) != expected[name]:
            raise SystemExit(f"Preparer differs from the validated asset: {name}")
    payload["SHA256SUMS.txt"] = "".join(
        f"{digest(data)}  {name}\n" for name, data in payload.items()
    ).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(payload):
            raise SystemExit("Tester ZIP failed verification")
        for name, data in payload.items():
            if archive.read(name) != data:
                raise SystemExit(f"Tester ZIP changed {name}")
    print(args.output, args.output.stat().st_size, digest(args.output.read_bytes()))


if __name__ == "__main__":
    main()
