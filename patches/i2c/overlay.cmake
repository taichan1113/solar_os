# PaperMono acceptance backport; replace one source in this project's target.
# Keep the installed ESP-IDF package and other boards unchanged.
if(NOT SOLAR_OS_BOARD STREQUAL "m5paper_mono")
    return()
endif()

set(i2c_overlay "${CMAKE_BINARY_DIR}/solar_os_i2c_rx_completion")
set(i2c_original "$ENV{IDF_PATH}/components/esp_driver_i2c")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_i2c_rx_completion.py"
        --idf "$ENV{IDF_PATH}" --output "${i2c_overlay}"
    RESULT_VARIABLE i2c_patch_result
    ERROR_VARIABLE i2c_patch_error
)
if(NOT i2c_patch_result EQUAL 0)
    message(FATAL_ERROR "I2C RX completion overlay failed: ${i2c_patch_error}")
endif()
idf_component_get_property(i2c_target esp_driver_i2c COMPONENT_LIB)
get_target_property(i2c_sources ${i2c_target} SOURCES)
set(replaced 0)
set(new_sources)
foreach(source IN LISTS i2c_sources)
    if(source MATCHES "(^|/)i2c_master.c$")
        list(APPEND new_sources "${i2c_overlay}/i2c_master.c")
        math(EXPR replaced "${replaced} + 1")
    else()
        list(APPEND new_sources "${source}")
    endif()
endforeach()
if(NOT replaced EQUAL 1)
    message(FATAL_ERROR "Expected one i2c_master.c source; found ${replaced}")
endif()
set_property(TARGET ${i2c_target} PROPERTY SOURCES ${new_sources})
target_include_directories(${i2c_target} PRIVATE "${i2c_original}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_i2c_rx_completion.py"
    "${i2c_original}/i2c_master.c")
