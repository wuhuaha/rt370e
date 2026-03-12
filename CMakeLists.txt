include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/river_build_info_bootstrap.cmake)

river_prepare_build_info_placeholders()

set(RIVER_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

ameba_add_subdirectory(components/river_common)
ameba_add_subdirectory(components/river_core)
ameba_add_subdirectory(components/river_voice)
ameba_add_subdirectory(components/river_cloud)
ameba_add_subdirectory(components/river_diag)
ameba_add_subdirectory(app)
