#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sh tests/host/run_launcher_touch_grid.sh
./tests/host/writer_d_async_test e1
./tests/host/writer_d_async_test e1-restore-fail
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.test_fastest_e1_flash_audit
