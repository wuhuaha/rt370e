if(NOT DEFINED c_MCU_PROJECT_NAME OR NOT c_MCU_PROJECT_NAME STREQUAL "hp")
    return()
endif()

if(NOT DEFINED c_CURRENT_IMAGE_TYPE OR NOT c_CURRENT_IMAGE_TYPE STREQUAL "image2")
    return()
endif()

if(NOT DEFINED RIVER_PROJECT_DIR OR RIVER_PROJECT_DIR STREQUAL "")
    message(FATAL_ERROR "RIVER_PROJECT_DIR is not set for HP Wi-Fi shim")
endif()

message(STATUS "ameba-river: adding HP Wi-Fi efuse bypass shim")
ameba_add_subdirectory(
    ${RIVER_PROJECT_DIR}/components/river_wifi_hp_shim
    river_wifi_hp_shim
)

message(STATUS "ameba-river: adding HP SDK compatibility shims")
ameba_add_subdirectory(
    ${RIVER_PROJECT_DIR}/components/river_hp_sdk_compat
    river_hp_sdk_compat
)
