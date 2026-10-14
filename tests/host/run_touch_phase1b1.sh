#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "${1:-}" = "--sanitize" ]; then
    make -B CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
        input_touch_service_test input_touch_adapter_test sketch_touch_projection_test input_test ft6336_frame_test input_touch_core_test ft6336_test ft6336_lifecycle_test
else
    make -B input_touch_service_test input_touch_adapter_test sketch_touch_projection_test input_test ft6336_frame_test input_touch_core_test ft6336_test ft6336_lifecycle_test
fi
./input_touch_service_test
./input_touch_adapter_test
./sketch_touch_projection_test
./input_test
./ft6336_frame_test
./input_touch_core_test
./ft6336_test
./ft6336_lifecycle_test
