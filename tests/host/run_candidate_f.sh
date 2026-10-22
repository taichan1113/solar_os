#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sh tests/host/run_ram_path_lut.sh
./tests/host/writer_d_async_test f
./tests/host/writer_d_async_test f-ram-path
./tests/host/writer_d_async_test f-restore-fail
