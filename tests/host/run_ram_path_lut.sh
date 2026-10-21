#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sh tests/host/run_standalone_sketch_lut.sh
./tests/host/writer_d_async_test ram-path
./tests/host/writer_d_async_test ram-full-restore-fail
