"""Independent SSD1677 Rev1.0 6.6/6.7, Table6-6/Fig6-6 encoding check.

Nominal source V.frame is NOT an effective DC balance or lifetime proof.
"""
import hashlib
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

def array(path, name):
    body = re.search(r'\b' + name + r'\[110\]\s*=\s*\{(.*?)\};', path.read_text(), re.S)[1]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    return bytes(int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b', body))

class BlackHoldLutTests(unittest.TestCase):
    def test_spec_encoding_and_unchanged_baseline(self):
        base = array(ROOT/'src/drivers/epd_ssd1677_reference.inc', 'ssd1677_reference_lut')
        trial = array(ROOT/'src/drivers/epd_ssd1677_waveform_trial.inc', 'ssd1677_trial_black_hold_single_lut')
        self.assertEqual(len(base), 110)
        self.assertEqual(len(trial), 110)
        self.assertEqual(hashlib.sha256(base).hexdigest(), 'e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50')
        # BF 00 vs baseline 00 00 changes offset 0 only. Offsets 0 and 1
        # change relative to old Candidate B (8F F0), not to baseline.
        self.assertEqual([i for i in range(110) if trial[i] != base[i]], [0])
        self.assertEqual(trial[:10], bytes([0xbf, 0x00] + [0]*8))
        self.assertEqual(trial[10:], base[10:])
        voltage = [0, 16, -15, 5]  # Table6-6 + unchanged 04 values
        def decode(lut):
            decoded = []
            for group in range(10):
                repetitions = lut[54 + group*5] + 1
                for phase in range(4):
                    frames = lut[50 + group*5 + phase]
                    code = (lut[group] // (4 ** (3-phase))) % 4
                    if frames:
                        decoded.append((group, phase, frames*repetitions, voltage[code]))
            return decoded
        decoded = decode(trial)
        self.assertEqual(decoded, [(0,0,2,-15),(0,1,2,5),(0,2,2,5),(0,3,2,5),(1,0,1,0),(1,1,1,0)])
        old_b = bytes([0x8f, 0xf0]) + base[2:]  # Archived B, never executable here.
        self.assertEqual([i for i in range(110) if trial[i] != old_b[i]], [0, 1])
        self.assertEqual(decode(old_b), [(0,0,2,-15),(0,1,2,0),(0,2,2,5),(0,3,2,5),(1,0,1,5),(1,1,1,5)])
        for v in voltage:
            self.assertEqual(sum(n for _,_,n,w in decoded if w == v),
                             sum(n for _,_,n,w in decode(old_b) if w == v))
        self.assertEqual(sum(n for _,_,n,_ in decoded), 10)
        self.assertEqual(sum(n*v for _,_,n,v in decoded), 0)
        self.assertEqual(trial[100:105], bytes([0x8f]*5))
        self.assertEqual(trial[105:], bytes([0x17,0x46,0xa8,0x32,0x30]))

if __name__ == '__main__':
    unittest.main()
