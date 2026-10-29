"""Make contact sheets from an audit report; generated game images stay under build/."""
import argparse
import gzip
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    if not out.is_relative_to(root / "build"):
        raise ValueError("Gallery must remain under ignored build/")
    out.mkdir(parents=True, exist_ok=True)
    rooms = json.loads(args.report.read_text(encoding="utf-8"))["rooms"]
    pages, cells = [], []
    for room in rooms:
        request = room.get("requested_case", {})
        for capture in room["capture_files"]:
            if not capture["valid"]:
                continue
            path = Path(capture["path"])
            cells.append((path, f"{room['world']}:{room['scene']} v{request.get('visit', 0)} bg{request.get('target_bg', 0)} {path.stem}", room))
    for first in range(0, len(cells), 12):
        sheet = Image.new("RGB", (1280, 660), "#181818")
        draw = ImageDraw.Draw(sheet)
        entries = []
        for slot, (path, label, room) in enumerate(cells[first:first+12]):
            data = gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_bytes()
            im = Image.open(io.BytesIO(data)).convert("RGB")
            im.thumbnail((316, 191))
            x, y = slot % 4 * 320, slot // 4 * 220
            sheet.paste(im, (x, y + 26))
            draw.text((x + 2, y + 2), label[:53], fill="white")
            caption = room.get("evidence_label") or ("Measured; visual review required" if room["evidence_complete"] else "Incomplete; visual review required")
            draw.text((x + 2, y + 14), caption, fill="#d1c18a" if room.get("evidence_label") or room["evidence_complete"] else "#ffb090")
            entries.append(dict(path=str(path), label=label))
        name = f"page-{first//12:03}.png"
        sheet.save(out / name)
        pages.append(dict(sheet=name, entries=entries, visual_status="unreviewed"))
    (out / "index.json").write_text(json.dumps(pages, indent=2), encoding="utf-8")
    print(f"{len(cells)} captures, {len(pages)} sheets; visual review still required")


if __name__ == "__main__":
    main()
