#pragma once

#include "solar_os.h"

extern const solar_os_app_t solar_os_writer_app;
#if defined(SOLAR_OS_SSD1677_WAVEFORM_TRIAL) && SOLAR_OS_SSD1677_WAVEFORM_TRIAL
void solar_os_writer_quality_command(solar_os_shell_io_t *, int, char **);
bool solar_os_writer_quality_active(void);
#endif

