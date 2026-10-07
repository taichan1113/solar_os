# Called after project(): replace only this project's bt target sources.
if(CONFIG_BT_ENABLED)
    foreach(required IN LISTS solar_os_nimble_required_config)
        if(NOT ${required})
            message(FATAL_ERROR
                "SolarOS BLE requires ${required}=y in the selected SDK configuration defaults. "
                "Active SDKCONFIG: ${SDKCONFIG}")
        endif()
    endforeach()
endif()

# Optional security diagnostics replace sources only in this project's build.
# SDK defaults and the installed package remain unchanged.
if(CONFIG_BT_NIMBLE_ENABLED AND "$ENV{SOLAR_OS_BLE_SECURITY_TRACE}" STREQUAL "1")
    set(security_overlay "${CMAKE_BINARY_DIR}/solar_os_nimble_security")
    set(nimble_host "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/src")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/trace_nimble_security.py"
        --idf "$ENV{IDF_PATH}" --output "${security_overlay}"
        --apply-fix
        RESULT_VARIABLE security_trace_result)
    if(NOT security_trace_result EQUAL 0)
        message(FATAL_ERROR "NimBLE security trace generation failed")
    endif()
    idf_component_get_property(bt_target bt COMPONENT_LIB)
    get_target_property(bt_sources ${bt_target} SOURCES)
    foreach(filename ble_sm.c ble_gap.c ble_sm_cmd.c)
        set(replaced 0)
        set(new_sources)
        foreach(source IN LISTS bt_sources)
            if(source MATCHES "(^|/)host/src/${filename}$")
                list(APPEND new_sources "${security_overlay}/${filename}")
                math(EXPR replaced "${replaced} + 1")
            else()
                list(APPEND new_sources "${source}")
            endif()
        endforeach()
        if(NOT replaced EQUAL 1)
            message(FATAL_ERROR "Expected one security trace ${filename}; found ${replaced}")
        endif()
        set(bt_sources ${new_sources})
    endforeach()
    set_property(TARGET ${bt_target} PROPERTY SOURCES ${bt_sources})
    target_include_directories(${bt_target} PRIVATE "${nimble_host}" "${CMAKE_CURRENT_LIST_DIR}/../../src/services")
    target_compile_definitions(${bt_target} PRIVATE SOLAR_OS_BLE_SECURITY_TRACE=1)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/trace_nimble_security.py"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_nimble_security.py"
        "${nimble_host}/ble_sm.c" "${nimble_host}/ble_gap.c" "${nimble_host}/ble_sm_cmd.c")
endif()

# The normal build uses only the minimal SM lifecycle fix, with no trace hooks.
if(CONFIG_BT_NIMBLE_ENABLED AND NOT "$ENV{SOLAR_OS_BLE_SECURITY_TRACE}" STREQUAL "1")
    set(security_overlay "${CMAKE_BINARY_DIR}/solar_os_nimble_security")
    set(nimble_host "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/src")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_nimble_security.py"
        --idf "$ENV{IDF_PATH}" --output "${security_overlay}"
        RESULT_VARIABLE security_fix_result)
    if(NOT security_fix_result EQUAL 0)
        message(FATAL_ERROR "NimBLE security lifecycle overlay generation failed")
    endif()
    idf_component_get_property(bt_target bt COMPONENT_LIB)
    get_target_property(bt_sources ${bt_target} SOURCES)
    set(replaced 0)
    set(new_sources)
    foreach(source IN LISTS bt_sources)
        if(source MATCHES "(^|/)host/src/ble_sm.c$")
            list(APPEND new_sources "${security_overlay}/ble_sm.c")
            math(EXPR replaced "${replaced} + 1")
        else()
            list(APPEND new_sources "${source}")
        endif()
    endforeach()
    if(NOT replaced EQUAL 1)
        message(FATAL_ERROR "Expected one ble_sm.c security source; found ${replaced}")
    endif()
    set_property(TARGET ${bt_target} PROPERTY SOURCES ${new_sources})
    target_include_directories(${bt_target} PRIVATE "${nimble_host}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_nimble_security.py" "${nimble_host}/ble_sm.c")
endif()

if(CONFIG_BT_NIMBLE_ENABLED AND CONFIG_BT_NIMBLE_DYNAMIC_SERVICE)
    set(nimble_overlay "${CMAKE_BINARY_DIR}/solar_os_nimble")
    set(nimble_host "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/nimble/host/src")
    execute_process(
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_nimble.py"
            --idf "$ENV{IDF_PATH}" --output "${nimble_overlay}"
        RESULT_VARIABLE overlay_result
        ERROR_VARIABLE overlay_error
    )
    if(NOT overlay_result EQUAL 0)
        message(FATAL_ERROR "${overlay_error}")
    endif()
    idf_component_get_property(bt_target bt COMPONENT_LIB)
    get_target_property(bt_sources ${bt_target} SOURCES)
    foreach(filename ble_gatts.c ble_att_svr.c)
        set(replaced 0)
        set(new_sources)
        foreach(source IN LISTS bt_sources)
            if(source MATCHES "(^|/)host/src/${filename}$")
                list(APPEND new_sources "${nimble_overlay}/${filename}")
                math(EXPR replaced "${replaced} + 1")
            else()
                list(APPEND new_sources "${source}")
            endif()
        endforeach()
        if(NOT replaced EQUAL 1)
            message(FATAL_ERROR "Expected exactly one NimBLE ${filename}; found ${replaced}")
        endif()
        set(bt_sources ${new_sources})
    endforeach()
    set_property(TARGET ${bt_target} PROPERTY SOURCES ${bt_sources})
    target_include_directories(${bt_target} PRIVATE "${nimble_host}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/patch_nimble.py"
        "${CMAKE_CURRENT_LIST_DIR}/dynamic_services.inc"
        "${CMAKE_CURRENT_LIST_DIR}/att_rollback.inc"
        "${nimble_host}/ble_gatts.c" "${nimble_host}/ble_att_svr.c")
endif()
