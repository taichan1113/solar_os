#!/bin/sh
set -eu
cd "$(dirname "$0")/worktree"
make -C tests/host epd_ssd1677_black_single_test black_single_shell_test epd_ssd1677_waveform_trial_test epd_ssd1677_black_repeat3_test black_repeat3_shell_test
./tests/host/epd_ssd1677_black_single_test
for mode in normal r2 fail-B fail-D restore-B restore-D abort-B abort-D cancel-B cancel-D; do
    printf 'RUN Shell %s\n' "$mode"
    ./tests/host/black_single_shell_test "$mode"
done
./tests/host/epd_ssd1677_waveform_trial_test
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v tests.test_black_hold_single_lut

./tests/host/epd_ssd1677_black_repeat3_test
for mode in normal r2 delay fail-A fail-D restore-A restore-D submit-A submit-D abort-A abort-D cancel-A cancel-D stop1-A stop1-D stop2-A stop2-D; do
    printf 'RUN repeat3 Shell %s\n' "$mode"
    ./tests/host/black_repeat3_shell_test "$mode"
done

make -C tests/host epd_ssd1677_black_prime_aa_test black_prime_aa_shell_test
./tests/host/epd_ssd1677_black_prime_aa_test
for mode in normal r2 delay clock fail1 fail2 fail3 restore1 restore2 restore3 submit1 submit2 submit3 abort1 abort2 abort3 stop-white stop0 stop1 stop2; do
    printf 'RUN prime Shell %s\n' "$mode"
    ./tests/host/black_prime_aa_shell_test "$mode"
done

make -C tests/host epd_ssd1677_black_dad_test black_dad_shell_test
./tests/host/epd_ssd1677_black_dad_test
for mode in normal r2 delay clock fail1 fail2 fail3 restore1 restore2 restore3 submit1 submit2 submit3 abort1 abort2 abort3 stop-white stop0 stop1 stop2; do
    printf 'RUN DAD Shell %s\n' "$mode"
    ./tests/host/black_dad_shell_test "$mode"
done
