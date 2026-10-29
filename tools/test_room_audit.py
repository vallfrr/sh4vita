"""Coverage accounting must never turn missing evidence into a pass."""
import json
import struct
import gzip
from pathlib import Path
import tempfile
import unittest

from room_audit import report, percentile, valid_bmp


class ReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.log = self.root / "sh4log.txt"
        self.census = dict(rooms=[dict(world=2, scene=5, asset="sb05.bin", eligible=True),
                                 dict(world=8, scene=22, asset="cm22.bin", eligible=False)],
                           extra_assets=[], limits=["Story variants untested"])
        # Valid-size TGA captures; visual content is deliberately not classified by the report.
        for name in ("a.tga", "b.tga"):
            header = bytearray(18); header[2] = 2
            struct.pack_into("<HHB", header, 12, 960, 544, 32)
            (self.root / name).write_bytes(header + bytes(960 * 544 * 4))

    def fixture(self, actual="2:5", completed=True, captures=True):
        lines = ["[audit] event=request index=1 frame=100 world=2 scene=5 entry=7"]
        for f in range(101, 141):
            lines += [f"[audit-render] frame={f} draws=123 clears=2 new_programs=0",
                      f"[audit] event=frame index=1 frame={f} world=2 scene=5 actual={actual} phase=measure ms=33.333 capture=0 heap_kb=100"]
        if captures:
            lines += [f"[audit] event=capture index=1 frame=120 world=2 scene=5 path=ux0:data/sh4/{n}.tga" for n in ("a", "b")]
        if completed:
            lines.append("[audit] event=complete index=1 frame=142 world=2 scene=5")
        return lines

    def result(self, lines):
        self.log.write_text("\n".join(lines), encoding="utf-8")
        return report(self.census, [self.log])

    def test_complete_keeps_visual_unreviewed(self):
        r = self.result(self.fixture())
        self.assertEqual(r["completed"], 1)
        self.assertEqual(r["eligible"], 1)
        self.assertEqual(r["catalog_rooms"], 2)
        self.assertEqual(r["rooms"][0]["draw_mean"], 123)
        self.assertEqual(r["rooms"][0]["visual_status"], "unreviewed")
        self.assertEqual(r["rooms"][0]["render_pass"]["gl_error_checks"], 0)
        self.assertIsNone(r["rooms"][0]["render_pass"]["gl_errors"])
        self.assertIsNone(r["rooms"][0]["render_pass"]["gxm_errors"])
        self.assertTrue(all(c["needs_dark_image_review"] for c in r["rooms"][0]["capture_files"]))
        self.assertEqual(len(r["missing"]), 0)
        self.assertEqual(len(r["excluded"]), 1)

    def test_wrong_room_is_not_measured(self):
        r = self.result(self.fixture(actual="2:4"))
        self.assertEqual(r["completed"], 0)
        self.assertEqual(r["rooms"][0]["measured_frames"], 0)

    def test_optional_driver_scene_errors_require_actual_checks(self):
        lines = self.fixture() + ["[audit-swap] frame=110 interval_us=33333 gl_error=0 gxm_error_checked=1 gxm_error=-7",
                                  "[audit-swap] frame=111 interval_us=33333 gl_error=0 gxm_error_checked=0 gxm_error=0"]
        r = self.result(lines)["rooms"][0]["render_pass"]
        self.assertEqual(r["gxm_error_checks"], 1)
        self.assertEqual(r["gxm_errors"], 1)

    def test_forced_background_requires_observed_match(self):
        lines = self.fixture()
        lines[0] += " target_bg=28"
        self.assertEqual(self.result(lines)["completed"], 0)
        lines = [s + " bg=28" if "event=frame" in s else s for s in lines]
        self.assertEqual(self.result(lines)["completed"], 1)
        lines[1] += " bg=27"
        # Another measured background must not be silently accepted.
        lines[2] = lines[2].replace("bg=28", "bg=27")
        self.assertEqual(self.result(lines)["completed"], 0)

    def test_crash_before_completion_is_not_covered(self):
        r = self.result(self.fixture(completed=False) + ["FATAL: [audit-error] vertex range outside buffer"])
        self.assertEqual(r["completed"], 0)
        self.assertEqual(len(r["alerts"]), 1)

    def test_unsupported_render_calls_are_reported(self):
        r = self.result(self.fixture() + ["[d3d8] SetTexture: unsupported texture type (cube/volume?)",
                                         "[d3d8] draw with function-less VS: ignored"])
        self.assertEqual(len(r["alerts"]), 2)
        self.assertTrue(all(x["category"] == "investigate" for x in r["alerts"]))

    def test_movie_memory_and_stage_cache_write_failures_are_reported(self):
        r = self.result(self.fixture() + ["[movie] output buffers: not enough memory",
                                         "[cg-cache] write failure 80010002 file=cache.gxp",
                                         "[tex] WARN: malloc 4096 KB failed for 1024x1024, texture skipped"])
        self.assertEqual(len(r["alerts"]), 3)
        self.assertTrue(all(x["category"] == "investigate" for x in r["alerts"]))

    def test_pc_notices_are_explicit_and_not_hidden_on_vita(self):
        lines = self.fixture() + ["[d3d8] CopyRects: destination too large, copy skipped",
                                  "[dinput] CreateDevice: unknown GUID 00000000"]
        self.assertTrue(all(x["category"] == "investigate" for x in self.result(lines)["alerts"]))
        (self.root / "config.json").write_text(json.dumps(dict(SH4_HEADLESS="1")))
        self.assertEqual([x["category"] for x in self.result(lines)["alerts"]], ["known-pc-copy-limit", "pc-no-controller"])

    def test_loading_shader_stall_remains_actionable(self):
        lines = self.fixture() + [
            "[audit] event=frame index=1 frame=150 world=2 scene=5 actual=2:5 phase=load ms=40 capture=0 control_blocked=1 link=0",
            "[audit-error] frame=150 no Present for 11000 ms",
            "[long] frame 150: 13574.2 ms (body 41.4 of which GL 0.0 links 13541.5 tex 0.0; disk 0.0 ms 0 KB; wait 0.0; sleep 0.0)",
            "[audit] event=frame index=1 frame=151 world=2 scene=5 actual=2:5 phase=load ms=13540 capture=0 link=13541.5"]
        result = self.result(lines)
        self.assertEqual(result["alerts"][0]["category"], "investigate")
        self.assertEqual(result["alerts"][0]["cause"], "shader-program-preparation")
        self.assertEqual(result["alerts"][0]["context"]["phase"], "load")
        self.assertEqual(result["rooms"][0]["loading_peak_ms"], 13540)
        self.assertEqual(result["rooms"][0]["shader_link_peak_ms"], 13541.5)
        self.assertLess(result["rooms"][0]["max_ms"], 40)  # steady performance stays separate

    def test_capture_stall_requires_a_completed_capture(self):
        lines = self.fixture() + ["[audit-error] frame=140 no Present for 11000 ms"]
        self.assertEqual(self.result(lines)["alerts"][0]["category"], "investigate")
        lines.append("[shot] ux0:data/sh4/b.tga")
        self.assertEqual(self.result(lines)["alerts"][0]["category"], "capture-overhead")

    def test_missing_captures_are_not_covered(self):
        self.assertEqual(self.result(self.fixture(captures=False))["completed"], 0)

    def test_truncated_capture_is_not_covered(self):
        (self.root / "b.tga").write_bytes(b"partial")
        self.assertEqual(self.result(self.fixture())["completed"], 0)

    def test_bmp_requires_full_pixel_array(self):
        data = bytearray(54 + 4 * 2 * 3)
        data[:2] = b"BM"
        struct.pack_into("<I", data, 2, len(data))
        struct.pack_into("<I", data, 10, 54)
        struct.pack_into("<IiiHHI", data, 14, 40, 2, 3, 1, 32, 0)
        self.assertTrue(valid_bmp(data))
        self.assertFalse(valid_bmp(data[:-1]))
        self.assertFalse(valid_bmp(data[:55]))

    def test_gzip_capture_crc_is_checked(self):
        data = gzip.compress((self.root / "b.tga").read_bytes())
        (self.root / "b.tga.gz").write_bytes(data)
        fixture = [s.replace("/b.tga", "/b.tga.gz") for s in self.fixture()]
        self.assertEqual(self.result(fixture)["completed"], 1)
        (self.root / "b.tga.gz").write_bytes(data[:-4])
        self.assertEqual(self.result(fixture)["completed"], 0)

    def test_capture_cost_excluded_and_stall_preserved(self):
        lines = self.fixture() + [
            "[audit] event=frame index=1 frame=150 world=2 scene=5 actual=2:5 phase=measure ms=9999 capture=1",
            "[audit] event=frame index=1 frame=151 world=2 scene=5 actual=2:5 phase=measure ms=500 capture=0"]
        r = self.result(lines)["rooms"][0]
        self.assertEqual(r["measured_frames"], 41)
        self.assertEqual(r["max_ms"], 500)
        self.assertEqual(r["over_100ms"], 1)

    def test_no_render_evidence_is_not_covered(self):
        self.assertEqual(self.result([s for s in self.fixture() if "audit-render" not in s])["completed"], 0)

    def test_script_and_movie_slowdowns_remain_visible_outside_steady_window(self):
        lines = self.fixture() + [
            "[audit] event=frame index=1 frame=150 world=2 scene=5 actual=2:5 phase=load ms=500 capture=0 control_blocked=1 script=7 movie=0",
            "[audit] event=frame index=1 frame=151 world=2 scene=5 actual=2:5 phase=movie ms=1000 capture=0 control_blocked=1 script=7 movie=1",
            "[audit] event=frame index=1 frame=152 world=2 scene=5 actual=2:5 phase=movie ms=9999 capture=1 control_blocked=1 script=7 movie=1",
            "[audit] event=frame index=1 frame=153 world=2 scene=5 actual=2:4 phase=movie ms=9999 capture=0 control_blocked=1 script=7 movie=1"]
        r = self.result(lines)["rooms"][0]
        self.assertEqual(r["state_windows"]["movie"]["frames"], 1)
        self.assertEqual(r["state_windows"]["movie"]["max_ms"], 1000)
        self.assertEqual(r["state_windows"]["control_blocked"]["frames"], 2)
        self.assertEqual(r["state_windows"]["scripted"]["worst_frame"]["frame"], "151")
        r = self.result(self.fixture())["rooms"][0]
        self.assertFalse(r["state_windows"]["movie"]["observed"])
        self.assertIsNone(r["state_windows"]["movie"]["max_ms"])

    def test_repeated_room_does_not_inherit_completion(self):
        first = self.fixture()
        second = [s.replace("index=1", "index=2") for s in self.fixture(completed=False)]
        second[0] += " visit=1 target_bg=0"
        r = self.result(first + second)
        self.assertEqual(len(r["rooms"]), 2)
        self.assertEqual(r["cases_completed"], 1)

    def test_percentile_and_empty_log(self):
        self.assertEqual(percentile(list(range(1, 101)), .95), 95)
        self.assertIsNone(percentile([], .95))
        r = self.result([])
        self.assertEqual(r["completed"], 0)
        self.assertEqual(len(r["missing"]), 1)
        json.dumps(r)


if __name__ == "__main__":
    unittest.main()
