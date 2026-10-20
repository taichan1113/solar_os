#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
make -C tests/host sketch_lut_panel_test
./tests/host/sketch_lut_panel_test
sh tests/host/run_sketch_d_diagnostics.sh
