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

# ---------------------------------------------------------------- the Shell
#
# `web/dist-shell` is the analysis app's own code and nothing else: index.html, the
# bundle, the WASI adapter, the manifest, the worker. The weights (Essentia, ONNX
# Runtime, TagLib, the models) are fetched from the CDN at run time and are not here
# (ADR 0048).
#
# It is packed into ONE image with tools/pack_shell.py and flashed to the `frontdoor`
# partition, so one `idf.py flash` writes firmware and page together. The board copies that
# image into PSRAM once at boot, before the panel exists, and serves it from there: a flash
# read takes the bus the RGB panel's scan-out draws from, and that is what tore the picture
# on every page load (ADR 0055).
#
# The image is the directory's bytes with a table of contents in front - the text files
# already gzipped by the build, the rest plain - and nothing on the board decompresses.
# Long hashed asset names no longer matter: the table stores lengths, not fixed-size names,
# so the SPIFFS object-name limit that used to be configured here is gone with SPIFFS.
#
# The partition's size is stated once, in partitions.csv.
#
# A checkout without the built page still builds: `/` then answers 503 ("this firmware
# carries no page"), /library and the API are unaffected. That is a deliberate
# allowance for firmware work that does not touch the page. Set XTAPE_REQUIRE_SHELL=1
# (as a CMake variable, -DXTAPE_REQUIRE_SHELL=1, or in the environment) to make it a
# configure error instead: a build meant to carry the page must fail without it, and
# an empty Shell is a mistake rather than a configuration.
set(XTAPE_SHELL_DIR "${CMAKE_CURRENT_LIST_DIR}/web/dist-shell"
    CACHE PATH "Directory packed into the frontdoor partition as the Shell (absolute)")
if(NOT DEFINED XTAPE_REQUIRE_SHELL AND DEFINED ENV{XTAPE_REQUIRE_SHELL})
    set(XTAPE_REQUIRE_SHELL "$ENV{XTAPE_REQUIRE_SHELL}")
endif()

if(EXISTS "${XTAPE_SHELL_DIR}")
    if(NOT IS_DIRECTORY "${XTAPE_SHELL_DIR}")
        message(FATAL_ERROR "XTAPE_SHELL_DIR is not a directory: ${XTAPE_SHELL_DIR}")
    endif()
    if(XTAPE_REQUIRE_SHELL AND NOT EXISTS "${XTAPE_SHELL_DIR}/index.html")
        message(FATAL_ERROR
            "XTAPE_REQUIRE_SHELL is set but ${XTAPE_SHELL_DIR} has no index.html: "
            "an empty Shell is not a configuration")
    endif()
    # The build log carries the commit the page came from (front-door spec F1), so a
    # flashed image can be matched to the source tree that produced it.
    execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse --short HEAD
                    OUTPUT_VARIABLE xtape_app_commit
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(NOT xtape_app_commit)
        set(xtape_app_commit "unknown")
    endif()
    message(STATUS "Shell: packing ${XTAPE_SHELL_DIR} into the frontdoor partition "
                   "(app commit ${xtape_app_commit})")

    set(XTAPE_SHELL_IMAGE "${CMAKE_BINARY_DIR}/shell.bin")
    set(XTAPE_SHELL_TOOL "${CMAKE_CURRENT_LIST_DIR}/tools/pack_shell.py")
    idf_build_get_property(xtape_python PYTHON)
    # Repacked whenever anything in the Shell changes: the image is derived, never edited.
    # CONFIGURE_DEPENDS is what keeps that true across a page rebuild: `npm run build:shell`
    # rehashes asset file names, so the list below changes without any file CMake tracks
    # being edited, and without it ninja goes on depending on assets that no longer exist.
    file(GLOB_RECURSE xtape_shell_files LIST_DIRECTORIES false CONFIGURE_DEPENDS
         "${XTAPE_SHELL_DIR}/*")
    add_custom_command(
        OUTPUT "${XTAPE_SHELL_IMAGE}"
        COMMAND "${xtape_python}" "${XTAPE_SHELL_TOOL}" "${XTAPE_SHELL_DIR}" "${XTAPE_SHELL_IMAGE}"
        DEPENDS ${xtape_shell_files} "${XTAPE_SHELL_TOOL}"
        COMMENT "Packing the Shell into one image"
        VERBATIM)
    add_custom_target(shell_image ALL DEPENDS "${XTAPE_SHELL_IMAGE}")
    esptool_py_flash_to_partition(flash "frontdoor" "${XTAPE_SHELL_IMAGE}")
    add_dependencies(flash shell_image)
elseif(XTAPE_REQUIRE_SHELL)
    message(FATAL_ERROR
        "XTAPE_REQUIRE_SHELL is set but ${XTAPE_SHELL_DIR} does not exist. Build the "
        "Shell first (see web/), or unset XTAPE_REQUIRE_SHELL to build a firmware whose "
        "`/` answers 503.")
else()
    message(WARNING
        "no Shell at ${XTAPE_SHELL_DIR}: the frontdoor partition will not be packed and "
        "GET / will answer 503 (\"this firmware carries no page\"). Build the Shell, then "
        "run `idf.py reconfigure` so this block sees it. Set -DXTAPE_REQUIRE_SHELL=1 to "
        "make a missing Shell a configure error.")
endif()
