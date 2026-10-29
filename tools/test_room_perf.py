import unittest
from room_perf import windows


class WindowTest(unittest.TestCase):
    def test_interpolation_counters_are_separate_from_logic_frame_count(self):
        text = "[stats] presents=100\n[perf] 33.3 ms/frame; frame body 20.00 ms\n"
        text += "[perf] frames > 36 ms: 1 of 60, longest 40.0 ms\n"
        text += "[q] interpolation: 55 intermediate frames, 0 cuts; intermediate pass 5.00 ms, pairing 1.00 ms (80 uploads), real 6.00 ms (GPU 0.00 / 0.00)\n"
        text += "[stats] presents=160\n"
        w = windows(text, 101, 160)[0]
        self.assertEqual(w["frames"], 60)
        self.assertEqual(w["intermediate_frames"], 55)
        self.assertEqual(w["real_submit_ms"], 6)

    def test_boundaries_captures_and_missing_start_are_excluded(self):
        text = "[stats] presents=100\n"
        for end in range(110, 151, 10):
            text += f"[perf] 40.0 ms/frame; frame body 39.90 ms\n[perf] frames > 36 ms: 10 of 10, longest 45.0 ms\n[stats] presents={end}\n"
        result = windows(text, 110, 145, [125])
        self.assertEqual([(w['first_frame'], w['last_frame']) for w in result], [(111, 120), (131, 140)])
        self.assertEqual(sum(w['frames'] for w in result), 20)
        self.assertEqual(windows(text, 0, 105), [])


if __name__ == '__main__':
    unittest.main()
