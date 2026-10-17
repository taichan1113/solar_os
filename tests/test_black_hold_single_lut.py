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
        self.assertEqual([i for i in range(110) if trial[i] != base[i]], [0, 1])
        self.assertEqual(trial[:10], bytes([0x2f, 0xf0] + [0]*8))
        self.assertEqual(trial[10:], base[10:])
        decoded = []
        voltage = [0, 16, -15, 5]  # Table6-6 + unchanged 04 values
        for group in range(10):
            repetitions = trial[54 + group*5] + 1
            for phase in range(4):
                frames = trial[50 + group*5 + phase]
                code = (trial[group] // (4 ** (3-phase))) % 4
                if frames:
                    decoded.append((group, phase, frames*repetitions, voltage[code]))
        self.assertEqual(decoded, [(0,0,2,0),(0,1,2,-15),(0,2,2,5),(0,3,2,5),(1,0,1,5),(1,1,1,5)])
        self.assertEqual(sum(n for _,_,n,_ in decoded), 10)
        self.assertEqual(sum(n*v for _,_,n,v in decoded), 0)
        self.assertEqual(trial[100:105], bytes([0x8f]*5))
        self.assertEqual(trial[105:], bytes([0x17,0x46,0xa8,0x32,0x30]))

    def test_B_to_D_only_reorders_first_two_phases(self):
        base = array(ROOT/'src/drivers/epd_ssd1677_reference.inc', 'ssd1677_reference_lut')
        d = array(ROOT/'src/drivers/epd_ssd1677_waveform_trial.inc', 'ssd1677_trial_black_hold_single_lut')
        b = bytes([0x8f, 0xf0]) + base[2:]
        self.assertEqual(hashlib.sha256(b).hexdigest(), '45c5419f6986e3b1a81f8394e386fd5bc627503dcaa6d2c483a9113a63209422')
        self.assertEqual([i for i in range(110) if b[i] != d[i]], [0])
        def decode(lut):
            result = []
            for group in range(10):
                for phase in range(4):
                    frames = lut[50+5*group+phase]*(lut[54+5*group]+1)
                    if frames:
                        result.append(((lut[group] >> (6-2*phase)) & 3, frames))
            return result
        self.assertEqual(decode(b), [(2,2),(0,2),(3,2),(3,2),(3,1),(3,1)])
        self.assertEqual(decode(d), [(0,2),(2,2),(3,2),(3,2),(3,1),(3,1)])
        for code in range(4):
            self.assertEqual(sum(n for c,n in decode(b) if c==code),
                             sum(n for c,n in decode(d) if c==code))
        self.assertEqual(sum(n for _,n in decode(b)), 10)
        self.assertEqual(sum(n for _,n in decode(d)), 10)

if __name__ == '__main__':
    unittest.main()
