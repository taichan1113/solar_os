#!/bin/sh
set -eu
cd "$(dirname "$0")"

if [ "${1:-}" = "--sanitize" ]; then
    make -B CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
        ft6336_frame_test input_touch_core_test ft6336_test ft6336_lifecycle_test
else
    make -B ft6336_frame_test input_touch_core_test ft6336_test ft6336_lifecycle_test
fi
./ft6336_frame_test
./input_touch_core_test
./ft6336_test
./ft6336_lifecycle_test
