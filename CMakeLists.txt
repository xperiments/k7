# The firmware project. Component sources live in components/; the host build in
# host/ compiles the portable ones against a shim instead of ESP-IDF.
cmake_minimum_required(VERSION 3.16)

# Two component roots, because the host build can compile only one of them:
#
#   portable/  every .c compiles on the host as well; H3 fails if one is missing
#              from host/Makefile.
#   app/       the screen, the entry policy and the board adapters: firmware only.
#              H3 checks that none of it leaks into the host build.
#
# The group directories are not components themselves. The exclude list keeps the
# default components/ scan from trying to register them; EXTRA_COMPONENT_DIRS is
# what discovers the components inside each group.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/portable"
    "${CMAKE_CURRENT_LIST_DIR}/components/app")
set(EXTRA_COMPONENT_EXCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/portable"
    "${CMAKE_CURRENT_LIST_DIR}/components/app")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The exclude list is read as a build property. Setting the variable above keeps
# the intent readable; this makes it effective on the IDF 6.1 build system.
idf_build_set_property(EXTRA_COMPONENT_EXCLUDE_DIRS
                       "${CMAKE_CURRENT_LIST_DIR}/components/portable;${CMAKE_CURRENT_LIST_DIR}/components/app")

project(sd_stream_server)

# LVGL inherits the project's -Og, and the interface's rendering is the hot path on the
# core it shares with the network stack: two rotating reel hubs with alpha, blended into
# an 800x480 framebuffer, every frame. Built optimised for the same reason the vendored
# encoder is - measured, not assumed, and the measurement is in the commit that added it.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -O2)
endif()
