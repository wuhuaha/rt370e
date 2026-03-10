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
    river_voice_board.c
    river_voice_frontend.c
    river_voice_echo.c
    river_voice_speaker_test.c
)

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
    ${c_CMPT_AUDIO_DIR}/interfaces
)

ameba_add_internal_library(river_voice
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)
