include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/river_build_info_bootstrap.cmake)

river_prepare_build_info_placeholders()

set(RIVER_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Keep extra shell headroom on the AP monitor in this debug branch without
# editing the SDK's non-prompt Kconfig symbol.
if(DEFINED c_CURRENT_IMAGE AND DEFINED c_MCU_PROJECT_NAME AND c_MCU_PROJECT_NAME STREQUAL "ap")
    set(RIVER_AP_MONITOR_TARGET monitor_${c_CURRENT_IMAGE})
    if(NOT TARGET ${RIVER_AP_MONITOR_TARGET})
        message(FATAL_ERROR "Expected AP monitor target not found: ${RIVER_AP_MONITOR_TARGET}")
    endif()
    target_compile_options(${RIVER_AP_MONITOR_TARGET} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${RIVER_PROJECT_DIR}/include/river/river_sdk_debug_overrides.h>"
        "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${RIVER_PROJECT_DIR}/include/river/river_sdk_debug_overrides.h>"
    )
endif()

ameba_add_subdirectory(components/river_common)
ameba_add_subdirectory(components/river_core)
ameba_add_subdirectory(components/river_voice)
ameba_add_subdirectory(components/river_cloud)
ameba_add_subdirectory(components/river_diag)
ameba_add_subdirectory(app)
