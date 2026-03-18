set(public_includes)
set(public_definitions)
set(public_libraries)

ameba_list_append(public_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_global_include(${public_includes})
ameba_global_define(${public_definitions})
ameba_global_library(${public_libraries})

set(private_sources)
set(private_includes)
set(private_definitions)
set(private_compile_options)

ameba_list_append(private_sources
    river_asr_iflytek_rtasr.c
    river_asr_provider_registry.c
    river_cloud_adapter.c
    river_online_control.c
    river_tts_iflytek_ws.c
    river_wifi_station.c
    river_ws_dispatch.c
)

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_add_internal_library(river_cloud
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)
