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
    river_app.c
    river_dialog_cloud_port.c
    river_interaction_state.c
    river_reset_trace.c
    river_session_coordinator.c
)

if(CONFIG_RIVER_INTERACTION_DIAG_EN)
    ameba_list_append(private_sources
        river_interaction_diag.c
    )
else()
    ameba_list_append(private_sources
        river_interaction_diag_stub.c
    )
endif()

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_add_internal_library(river_core
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)
