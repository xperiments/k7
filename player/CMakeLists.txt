# The K7Player's firmware: its own ESP-IDF project on the classic ESP32 (ADR 0097), beside
# the K7's — two builds, not one build with two targets.
#
# It knows two component directories and no more: its own, and the shared portable
# components it actually uses, linked read-only from ../components/portable. Nothing of the
# K7's UI, card or panel is on this path, and the layering check in player/host/tests fails
# the build if that changes.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/handover"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/espnow_stream"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/player_update"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/device_name"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/tone_eq"
    "${CMAKE_CURRENT_LIST_DIR}/../components/portable/dr_libs")

# The version is the repository's, as the K7's is (ADR 0079): the descriptor, the update's
# header and the manifest must name the same number, and a player that reported a git-describe
# string would answer a probe with a version no header can match.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" K7_VERSION)
string(STRIP "${K7_VERSION}" K7_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/../VERSION")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(k7player VERSION ${K7_VERSION})
