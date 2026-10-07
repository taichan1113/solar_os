# Native RX8130CE is registered by the manifest expansion device, not a
# second board-level singleton. Calendar/time behavior uses generic services.
set(SOLAR_OS_BOARD_RTC_DRIVER "rx8130ce")
list(APPEND SOLAR_OS_BOARD_REQUIRED_PACKAGES driver_rx8130ce)
