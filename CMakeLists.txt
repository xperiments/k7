# The firmware project. Component sources live in components/; the host build in
# host/ compiles the same component sources against a shim instead of ESP-IDF.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sd_stream_server)

# LVGL inherits the project's -Og, and the interface's rendering is the hot path on the
# core it shares with the network stack: two rotating reel hubs with alpha, blended into
# an 800x480 framebuffer, every frame. Built optimised for the same reason the vendored
# encoder is - measured, not assumed, and the measurement is in the commit that added it.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -O2)
endif()
