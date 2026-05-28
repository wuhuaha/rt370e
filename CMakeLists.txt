include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/river_build_info_bootstrap.cmake)

river_prepare_build_info_placeholders()

set(RIVER_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED c_MCU_PROJECT_NAME AND c_MCU_PROJECT_NAME STREQUAL "ap")
    set(CMAKE_PROJECT_hp_INCLUDE
        ${RIVER_PROJECT_DIR}/cmake/river_hp_project_hook.cmake
        CACHE FILEPATH "ameba-river HP project hook" FORCE
    )
endif()

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

function(river_apply_audio_hal_overrides target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Expected AP audio HAL target not found: ${target}")
    endif()
    get_property(header GLOBAL PROPERTY RIVER_AUDIO_HW_OVERRIDES_HEADER)
    if(NOT header)
        message(FATAL_ERROR "Expected RIVER_AUDIO_HW_OVERRIDES_HEADER to be set")
    endif()
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${header}>"
    )
endfunction()

if(DEFINED c_CURRENT_IMAGE AND DEFINED c_MCU_PROJECT_NAME AND c_MCU_PROJECT_NAME STREQUAL "ap")
    set_property(GLOBAL PROPERTY RIVER_AUDIO_HW_OVERRIDES_HEADER
        "${RIVER_PROJECT_DIR}/include/river/river_audio_hw_overrides.h"
    )
    cmake_language(DEFER
        DIRECTORY "${c_SOC_PROJECT_DIR}/project_ap/make/image2"
        CALL river_apply_audio_hal_overrides
            "audio_hal_${c_CURRENT_IMAGE}"
    )
endif()

ameba_add_subdirectory(components/river_common)
ameba_add_subdirectory(components/river_core)
ameba_add_subdirectory(components/river_voice)
ameba_add_subdirectory(components/river_cloud)
ameba_add_subdirectory(components/river_diag)
ameba_add_subdirectory(app)
