set(public_includes)
set(public_definitions)
set(public_libraries)

set(river_opus_component_dir
    ${RIVER_PROJECT_DIR}/../ameba-rtos-1.2/component/audio/third_party/libopus
)

ameba_list_append(public_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_global_include(${public_includes})
ameba_global_define(${public_definitions})
ameba_global_library(${public_libraries})

if(CONFIG_RIVER_CLOUD_BACKEND_XIAOZHI_REALTIME AND
   EXISTS ${river_opus_component_dir}/CMakeLists.txt)
    ameba_add_subdirectory(${river_opus_component_dir} libopus_for_river)
endif()

set(private_sources)
set(private_includes)
set(private_definitions)
set(private_compile_options)

ameba_list_append(private_sources
    river_cloud_adapter.c
    river_online_control.c
    river_wifi_station.c
    river_ws_dispatch.c
)

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
)

if(CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT)
    ameba_list_append(private_sources
        river_asr_iflytek_rtasr.c
        river_asr_provider_registry.c
        river_tts_iflytek_ws.c
    )
endif()

if(CONFIG_RIVER_CLOUD_BACKEND_XIAOZHI_REALTIME)
    ameba_list_append(private_sources
        river_opus_codec.c
        river_xiaozhi_mcp_bridge.c
        river_xiaozhi_ws.c
    )
    ameba_list_append(private_includes
        ${river_opus_component_dir}/include
    )
endif()

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
