from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('i2c_patch', ROOT / 'scripts/patch_i2c_rx_completion.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

# The production ISR's completion prefix, with the remaining asynchronous path
# outside this test. Queue notification immediately runs the other CPU's caller.
PREFIX = '''static void i2c_master_isr_handler_default(void *arg)
{
    i2c_master_bus_t *i2c_master = arg;
    BaseType_t HPTaskAwoken = 0;
    uint32_t int_mask = I2C_LL_INTR_MST_COMPLETE;
    i2c_master->event = I2C_EVENT_DONE;
'''
FIXTURE = PREFIX + patch.NOTIFY + patch.RECEIVE + '''
    if (i2c_master->async_trans) {
    }
}
'''


class I2cRxCompletionTest(unittest.TestCase):
    def run_completion(self, source):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('A host C compiler is required for the cross-CPU interleaving test')
        prefix = source[source.index('static void i2c_master_isr_handler_default'):]
        prefix = prefix[:prefix.index('    if (i2c_master->async_trans)')] + '}\n'
        harness = '''#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int BaseType_t;
enum { I2C_EVENT_ALIVE, I2C_EVENT_DONE, I2C_EVENT_NACK, I2C_EVENT_TIMEOUT };
enum { I2C_LL_INTR_MST_COMPLETE = 1, I2C_LL_INTR_END_DETECT = 2 };
typedef struct { int event; bool contains_read; void *event_queue; uint8_t *rx_target; } i2c_master_bus_t;
static i2c_master_bus_t bus;
static uint8_t payload, reused;
static bool caller_observed_complete;
static unsigned notifications;
static void i2c_isr_receive_handler(i2c_master_bus_t *b) { *b->rx_target = 42; }
static int xQueueSendFromISR(void *q, void *event, BaseType_t *wake) {
    (void)q; (void)event; (void)wake;
    caller_observed_complete = payload == 42;
    bus.rx_target = &reused; /* Caller starts the next transaction on CPU 1. */
    notifications++;
    return 1;
}
'''
        harness += prefix + '''int main(void) {
    bus.rx_target = &payload; bus.contains_read = true;
    i2c_master_isr_handler_default(&bus);
    if (notifications != 1) return 2;
    puts(caller_observed_complete && payload == 42 && reused == 0 ? "complete" : "stale");
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            cfile, executable = folder / 'interleaving.c', folder / 'interleaving'
            cfile.write_text(harness)
            compiled = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                       str(cfile), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout.strip()

    def test_other_cpu_cannot_reuse_rx_storage_before_fifo_copy(self):
        self.assertEqual(self.run_completion(FIXTURE), 'stale')
        self.assertEqual(self.run_completion(patch.patch_source(FIXTURE)), 'complete')

    def test_exact_backport_is_idempotent(self):
        fixed = patch.patch_source(FIXTURE)
        self.assertEqual(fixed, PREFIX + patch.RECEIVE + patch.COMMENT + patch.NOTIFY
                         + FIXTURE[FIXTURE.index('    if (i2c_master->async_trans)') - 1:])
        self.assertEqual(patch.patch_source(fixed), fixed)

    def test_unknown_sdk_layout_is_rejected(self):
        for source in (FIXTURE.replace('static void', 'static int'),
                       FIXTURE.replace('contains_read == true', 'contains_read'),
                       FIXTURE + FIXTURE):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    patch.patch_source(source)

    def test_cli_changes_only_build_local_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            sdk = folder / 'sdk'
            original = sdk / 'components/esp_driver_i2c/i2c_master.c'
            original.parent.mkdir(parents=True)
            original.write_text(FIXTURE)
            output = folder / 'build'
            result = subprocess.run([shutil.which('python3') or 'python', str(ROOT / 'scripts/patch_i2c_rx_completion.py'),
                                     '--idf', str(sdk), '--output', str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(original.read_text(), FIXTURE)
            self.assertEqual((output / original.name).read_text(), patch.patch_source(FIXTURE))

    def test_other_board_does_not_load_overlay(self):
        cmake = shutil.which('cmake')
        if cmake is None:
            self.skipTest('CMake is required')
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'other_board.cmake'
            script.write_text('set(SOLAR_OS_BOARD solar_term)\n'
                              + f'include("{(ROOT / "patches/i2c/overlay.cmake").as_posix()}")\n')
            result = subprocess.run([cmake, '-P', str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
