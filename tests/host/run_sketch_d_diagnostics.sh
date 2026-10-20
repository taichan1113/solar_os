#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sh tests/host/run_fastest_e1_trial.sh
./tests/host/writer_d_async_test sketch-d
./tests/host/writer_d_async_test sketch-d-restore-fail
