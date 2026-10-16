# The firmware project. Component sources live in components/; the host build in
# host/ compiles the same component sources against a shim instead of ESP-IDF.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sd_stream_server)
