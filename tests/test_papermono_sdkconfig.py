from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / 'scripts/papermono_sdkconfig.cmake'
SETTINGS = {
    'CONFIG_PM_SLP_IRAM_OPT': 'y',
    'CONFIG_PM_RTOS_IDLE_OPT': 'y',
    'CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP': 'n',
    'CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION': 'y',
}


class PaperMonoSdkconfigTest(unittest.TestCase):
    def run_guard(self, contents, board='m5paper_mono', name='sdkconfig'):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            config = folder / name
            if contents is not None:
                config.write_text(contents)
            script = folder / 'check.cmake'
            selection = (f'set(SOLAR_OS_BOARD "{board}")\n' if board is not None else
                         'set(ENV{SOLAR_OS_BOARD} "m5paper_mono")\n')
            script.write_text(
                selection +
                f'set(SDKCONFIG "{config.as_posix()}")\n'
                'set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.local")\n'
                f'include("{GUARD.as_posix()}")\n'
                + 'solar_os_configure_papermono_sleep()\n' * 2
                + 'file(WRITE "defaults.txt" "${SDKCONFIG_DEFAULTS}")\n'
            )
            result = subprocess.run(['cmake', '-P', str(script)], cwd=folder,
                                    capture_output=True, text=True)
            return result, config.read_text() if config.exists() else None, (
                (folder / 'defaults.txt').read_text() if (folder / 'defaults.txt').exists() else '')

    def test_migration_preserves_unrelated_values_and_is_idempotent(self):
        for spelling in ('disabled-comment', 'explicit-value', 'missing'):
            before = 'CONFIG_LOCAL_SETTING=42\nCONFIG_OTHER=y\n'
            for key, value in SETTINGS.items():
                if spelling == 'disabled-comment':
                    before += f'# {key} is not set\n'
                elif spelling == 'explicit-value':
                    before += f'{key}={"n" if value == "y" else "y"}\n'
            result, after, defaults = self.run_guard(before)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('CONFIG_LOCAL_SETTING=42\nCONFIG_OTHER=y\n', after)
            for key, value in SETTINGS.items():
                line = f'{key}=y' if value == 'y' else f'# {key} is not set'
                self.assertEqual(after.splitlines().count(line), 1)
            self.assertEqual(defaults.split(';')[:2], ['sdkconfig.defaults', 'sdkconfig.local'])
            self.assertEqual(defaults.count('papermono_sleep.defaults'), 1)
            self.assertEqual(result.stderr.count('configuring PaperMono'), 1)

    def test_other_boards_are_unchanged(self):
        for board in ('esp32_s3_devkitc1_n16r8', 'solar_term', 'm5paper_mono_bringup'):
            before = 'CONFIG_PM_SLP_IRAM_OPT=n\nCONFIG_LOCAL_SETTING=42\n'
            result, after, defaults = self.run_guard(before, board)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(after, before)
            self.assertEqual(defaults, 'sdkconfig.defaults;sdkconfig.local')

    def test_fresh_config_gets_board_defaults_without_creating_sdkconfig(self):
        result, after, defaults = self.run_guard(None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(after)
        self.assertIn('papermono_sleep.defaults', defaults)

    def test_environment_board_selection_is_supported(self):
        result, after, defaults = self.run_guard('CONFIG_LOCAL_SETTING=42\n', board=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y', after)
        self.assertIn('papermono_sleep.defaults', defaults)

    def test_defaults_cannot_be_modified_as_generated_config(self):
        before = 'CONFIG_LOCAL_SETTING=42\n'
        result, after, _ = self.run_guard(before, name='sdkconfig.defaults.local')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not defaults', result.stderr)
        self.assertEqual(after, before)


if __name__ == '__main__':
    unittest.main()
