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
    river_voice_capture.c
    river_voice_detector.c
    river_voice_detector_silero.cc
    river_voice_frontend.c
    river_playback_service.c
    river_playback_frame_pool.c
    river_reference_service.c
    river_voice_segment_buffer.c
    river_voice_segment_sink.c
    river_voice_vad_probe.c
    river_voice_experiment.c
    river_voice_frame_pool.c
    river_voice_preproc.c
    river_voice_profile.c
    river_voice_runtime_policy.c
    river_voice_preproc_fixed_dsb.c
    river_voice_ref.c
    ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/complex_fft.c
    ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/real_fft.c
    ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/signal_processing_library.cc
    generated/river_silero_vad_model_data.cc
)

if(CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN STREQUAL "y")
    ameba_list_append(private_sources
        river_voice_echo.c
    )
else()
    ameba_list_append(private_sources
        river_voice_echo_stub.c
    )
endif()

if(CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN STREQUAL "y")
    ameba_list_append(private_sources
        river_voice_webrtc_aecm_adapter.c
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/aecm_core.cc
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/aecm_core_c.cc
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/delay_estimator.cc
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/delay_estimator_wrapper.cc
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/echo_control_mobile.cc
        ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm/ring_buffer.c
    )
endif()

if(CONFIG_RIVER_VOICE_CAPABILITY_KWS)
    ameba_list_append(private_sources
        river_voice_kws.cc
    )
    if(CONFIG_RIVER_KWS_MEAN_PATCH_EN STREQUAL "y")
        ameba_list_append(private_sources
            river_voice_kws_mean_patch.cc
        )
    endif()
endif()

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
    ${c_CMPT_AUDIO_DIR}/interfaces
    ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm
    ${RIVER_PROJECT_DIR}/third_party/webrtc_aecm/aecm
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
        -Wno-unused-parameter
)
