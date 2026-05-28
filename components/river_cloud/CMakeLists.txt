set(public_includes)
set(public_definitions)
set(public_libraries)

if(DEFINED ENV{AMEBA_SDK_ROOT} AND NOT "$ENV{AMEBA_SDK_ROOT}" STREQUAL "")
    set(RIVER_SDK_ROOT $ENV{AMEBA_SDK_ROOT})
else()
    set(RIVER_SDK_ROOT /root/ameba-rtos)
endif()

set(river_opus_component_dir
    ${RIVER_SDK_ROOT}/component/audio/third_party/libopus
)

ameba_list_append(public_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_global_include(${public_includes})
ameba_global_define(${public_definitions})
ameba_global_library(${public_libraries})

if(CONFIG_RIVER_ORVIBO_CLIENT_EN AND
   EXISTS ${river_opus_component_dir}/CMakeLists.txt)
    ameba_add_subdirectory(${river_opus_component_dir} libopus_for_river)
endif()

set(private_sources)
set(private_includes)
set(private_definitions)
set(private_compile_options)

ameba_list_append(private_sources
    river_wifi_station.c
    river_wifi_init_override.c
    river_ws_handshake.c
    river_ws_dispatch.c
    river_opus_codec.c
    river_orvibo_build_info.c
    river_orvibo_access.c
    river_orvibo_protocol.c
    river_orvibo_mcp_volume.c
)

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
    ${c_CMPT_AUDIO_DIR}/interfaces
    ${c_CMPT_NETWORK_DIR}
)

ameba_list_append(private_includes
    ${river_opus_component_dir}/include
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

if(CONFIG_RIVER_ORVIBO_CLIENT_EN AND c_CURRENT_IMAGE)
    ameba_target_link_options(${c_CURRENT_IMAGE}
        "-Wl,--wrap=ws_client_handshake"
        "-Wl,--wrap=wifi_init"
        p_SCOPE private
    )
endif()
