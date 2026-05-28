set(private_sources)
set(private_includes)

ameba_list_append(private_sources
    river_wifi_hp_efuse_bypass.c
)

ameba_list_append(private_includes
    ${c_CMPT_SOC_DIR}/fwlib/include
    ${c_CMPT_SOC_DIR}/swlib
)

ameba_add_internal_library(river_wifi_hp_shim
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
)

if(c_CURRENT_IMAGE)
    ameba_target_link_options(${c_CURRENT_IMAGE}
        "-Wl,--wrap=wifi_on"
        "-Wl,--wrap=LOGUART_INTConfig"
        "-Wl,--wrap=LOGUART_Readable"
        p_SCOPE private
    )
endif()
