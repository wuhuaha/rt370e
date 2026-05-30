set(private_sources)
set(private_includes)

ameba_list_append(private_sources
    river_hp_lvgl_demo_compat.c
)

ameba_list_append(private_includes
    ${c_CMPT_SOC_DIR}/fwlib/include
    ${c_CMPT_SOC_DIR}/swlib
    ${c_CMPT_UI_DIR}/display
)

ameba_add_internal_library(river_hp_sdk_compat
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
)
