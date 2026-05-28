if(NOT PROJECT_NAME STREQUAL "hp")
    return()
endif()

get_filename_component(RIVER_PROJECT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

set(c_CMPT_EXAMPLE_DIR
    "${RIVER_PROJECT_DIR}/cmake/river_hp_example"
)

message(STATUS "ameba-river: HP project hook active")
