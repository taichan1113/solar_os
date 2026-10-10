"""Check the diagnostic schedule without hardware; never claim optical PASS."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


class FakeGfx:
    WHITE, BLACK, FONT_MONO_12, FONT_BOLD_20, FONT_MONO_20, KEY_ESCAPE = range(6)

    def __init__(self, size=(800, 480)):
        self.calls = []
        self.dimensions = size
        self.keys = iter((32, 32, ord("q")))

    def __getattr__(self, name):
        return lambda *args: self.calls.append((name, args))

    def size(self):
        return self.dimensions

    def getch(self, timeout):
        return next(self.keys)


def load(gfx):
    mock = types.SimpleNamespace(gfx=gfx, should_exit=lambda: False,
        time=types.SimpleNamespace(uptime_ms=lambda: 100, sleep_ms=lambda ms: None))
    path = Path(__file__).parent / "hil" / "papermono_fastest_quality.py"
    spec = importlib.util.spec_from_file_location("quality_hil", path)
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, solaros=mock):
        spec.loader.exec_module(module)
    return module


class QualityHarnessTests(unittest.TestCase):
    def test_each_schedule_keeps_requested_count_and_releases_target(self):
        for pattern in ("hold", "cycle", "add", "erase"):
            gfx = FakeGfx()
            module = load(gfx)
            with patch.object(sys, "argv", ["quality.py", pattern, "10"]), patch("builtins.print"):
                module.main()
            self.assertEqual(sum(n == "present" for n, _ in gfx.calls), 11) # white setup + 10 trials
            self.assertEqual(gfx.calls[0], ("begin", ("display0",)))
            self.assertEqual(gfx.calls[-1], ("end", ()))
            self.assertEqual(sum(n == "clear" for n, _ in gfx.calls), 2)
            # Left retained text must appear only in the initial frame.
            self.assertEqual(sum(n == "text" and a[0] == 32 and a[1] == 54 for n, a in gfx.calls), 1)

    def test_rejects_long_unattended_run_before_claiming(self):
        gfx = FakeGfx()
        module = load(gfx)
        with patch.object(sys, "argv", ["quality.py", "hold", "100"]):
            with self.assertRaises(ValueError):
                module.main()
        self.assertEqual(gfx.calls, [])

    def test_bad_size_releases_claim(self):
        gfx = FakeGfx(size=(480, 800))
        module = load(gfx)
        with patch.object(sys, "argv", ["quality.py"]):
            with self.assertRaises(ValueError):
                module.main()
        self.assertEqual(gfx.calls, [("begin", ("display0",)), ("end", ())])

    def test_user_can_stop_after_white_setup(self):
        gfx = FakeGfx()
        gfx.keys = iter((ord("q"),))
        module = load(gfx)
        with patch.object(sys, "argv", ["quality.py"]), patch("builtins.print"):
            module.main()
        self.assertEqual(sum(n == "present" for n, _ in gfx.calls), 1)
        self.assertEqual(gfx.calls[-1], ("end", ()))


if __name__ == "__main__":
    unittest.main()
