#!/bin/sh
set -eu
cd "$(dirname "$0")"
sh ./run_touch_phase1b1.sh "${1:-}"
if [ "${1:-}" = "--sanitize" ]; then
    make -B CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' ft6336_raw_test ft6336_raw_shell_test
else
    make -B ft6336_raw_test ft6336_raw_shell_test
fi
./ft6336_raw_test
./ft6336_raw_shell_test
