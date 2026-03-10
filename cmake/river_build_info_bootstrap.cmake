function(river_prepare_build_info_placeholders)
    set(_river_menuconfig_root "${CMAKE_BINARY_DIR}/../menuconfig")

    string(TIMESTAMP _river_build_time "%Y-%m-%d %H:%M:%S")
    string(TIMESTAMP _river_build_date "%Y-%m-%d")

    foreach(_river_mcu project_ap project_hp project_lp)
        file(MAKE_DIRECTORY "${_river_menuconfig_root}/${_river_mcu}")
        file(WRITE "${_river_menuconfig_root}/${_river_mcu}/build_info.h"
"#ifndef AMEBA_RIVER_BUILD_INFO_H
#define AMEBA_RIVER_BUILD_INFO_H

/* Project-side bootstrap header.
 * The RTL8730E SDK may compile some objects before its generated build_info.h exists.
 * Keep these macros available from configure time so parallel builds remain stable.
 */
#define UTS_VERSION \"${_river_build_time}\"
#define RTL_FW_COMPILE_TIME \"${_river_build_time}\"
#define RTL_FW_COMPILE_DATE \"${_river_build_date}\"
#define RTL_FW_COMPILE_BY \"river\"
#define RTL_FW_COMPILE_HOST \"external-project\"
#define RTL_FW_COMPILE_DOMAIN \"external-project\"
#define RTL_FW_COMPILER \"bootstrap\"

#endif
")
    endforeach()
endfunction()
