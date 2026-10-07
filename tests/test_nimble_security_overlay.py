from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import patch_nimble_security as patch
import trace_nimble_security as trace
import test_nimble_security as native

SDK = ROOT / '.pio-core/packages/framework-espidf'

class NimbleSecurityOverlayTest(unittest.TestCase):
    def raw(self):
        source = SDK / patch.HOST / 'ble_sm.c'
        if not source.exists():
            self.skipTest('ESP-IDF 5.5.5 source required for actual-SDK regression')
        return source.read_bytes()

    def test_unknown_sdk_is_rejected(self):
        with self.assertRaises(ValueError): patch.transform(b'unsupported SDK')
        with self.assertRaises(ValueError): trace.transform('ble_sm.c',b'unsupported SDK')

    def test_only_encryption_initiate_changes(self):
        original = self.raw().decode('utf-8')
        fixed = patch.transform(self.raw())
        self.assertEqual(native.extract(original,'ble_sm_process_result'),native.extract(fixed,'ble_sm_process_result'))
        self.assertEqual(native.extract(original,'ble_sm_enc_event_rx'),native.extract(fixed,'ble_sm_enc_event_rx'))
        self.assertNotEqual(native.extract(original,'ble_sm_enc_initiate'),native.extract(fixed,'ble_sm_enc_initiate'))

    def test_generation_preserves_installed_source_and_is_repeatable(self):
        raw = self.raw()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            patch.generate(SDK, output)
            first = (output/'ble_sm.c').read_bytes()
            patch.generate(SDK, output)
            self.assertEqual(first,(output/'ble_sm.c').read_bytes())
            self.assertEqual(raw,self.raw())
        with self.assertRaises(ValueError): patch.generate(SDK,SDK/'must-not-write')

    def test_trace_can_compare_stock_and_fixed_without_changing_input(self):
        raw = self.raw()
        before = trace.transform('ble_sm.c',raw)
        after = trace.transform('ble_sm.c',raw,apply_fix=True)
        self.assertIn('SM_TRACE("sm_remove"',before)
        self.assertIn('SM_TRACE("hci_encrypt_rx"',after)
        self.assertIn('return BLE_HS_EALREADY;',native.extract(after,'ble_sm_enc_initiate'))
        self.assertEqual(raw,self.raw())

    @unittest.skipUnless(sys.platform != 'win32' and shutil.which('gcc'), 'Run native actual-SDK regression under Linux/WSL with gcc')
    def test_actual_sdk_before_fails_after_passes(self):
        raw = self.raw()
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory)/'before.c'
            fixed = Path(directory)/'after.c'
            original.write_bytes(raw)
            fixed.write_text(patch.transform(raw),encoding='utf-8')
            self.assertNotEqual(native.run(original,'gcc',failure_sequence=True),0)
            self.assertEqual(native.run(fixed,'gcc',failure_sequence=True),0)
            self.assertEqual(native.run(fixed,'gcc'),0)

if __name__ == '__main__': unittest.main()
