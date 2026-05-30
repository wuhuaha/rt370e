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
    river_orvibo_ui.c
)

ameba_list_append_if(CONFIG_RIVER_UI_LVGL_EN private_sources
    river_lv_font_zh_16.c
    river_lvgl_port.c
    river_sdk_lvgl_demo_compat.c
    river_st7102_mipi.c
)

ameba_list_append_if(CONFIG_RIVER_UI_TOUCH_PROBE_EN private_sources
    river_touch_sitronix_probe.c
)

ameba_list_append(private_includes
    ${RIVER_PROJECT_DIR}/include
)

ameba_list_append_if(CONFIG_RIVER_UI_LVGL_EN private_includes
    ${c_CMPT_UI_DIR}/LVGL/lvgl-9.3/lvgl
    ${c_CMPT_UI_DIR}/LVGL/lvgl-9.3/port/${c_SOC_TYPE}
    ${c_CMPT_UI_DIR}/display
    ${c_CMPT_UI_DIR}/display/controller/include
)

ameba_list_append(private_compile_options
    -Wno-error
)

ameba_add_internal_library(river_ui
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)
