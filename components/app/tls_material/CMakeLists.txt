# The shipped TLS pair, compiled into every image.
#
# There is nothing to generate on the device and nothing to install beyond the
# browser's one trust step: the leaf below is signed once by the CA whose key stays
# offline (tls/ca.key is never embedded and never flashed), it is valid for
# xtape.local plus localhost/127.0.0.1 for five years, and it is the same pair in
# every firmware (front door T1, T1a; ADR 0048).
#
# CMake embeds the bytes and this component hands them to the http server as
# const uint8_t[] + length. The names CMake derives come from the file's base name,
# so the paths may be absolute and outside the component - which they are: the pair
# lives in tls/ at the repository root, where a rotation changes one file.
set(tls_dir "${CMAKE_CURRENT_LIST_DIR}/../../../tls")
set(tls_cert "${tls_dir}/xtape.local.pem")
set(tls_key "${tls_dir}/xtape.local.key")

set(tls_embed "")
if(EXISTS "${tls_cert}" AND EXISTS "${tls_key}")
    # EMBED_TXTFILES appends a NUL, which both PEM parsers accept after the END
    # line. It also makes the pair an object dependency, so editing it relinks the
    # firmware instead of leaving the old one in the image.
    set(tls_embed "${tls_cert}" "${tls_key}")
    message(STATUS "tls_material: embedding ${tls_cert} and its key")
else()
    # Not a configure error. A firmware with no pair still boots and still serves
    # http://xtape.local/stream; the http server logs why the app has no TLS and
    # starts only the plain listener (front door section 6).
    message(WARNING
        "tls_material: ${tls_cert} or ${tls_key} is missing - this image will have no "
        "HTTPS listener and will serve the stream on port 80 only")
endif()

idf_component_register(SRCS "tls_material.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES ${tls_embed})

if(tls_embed)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TLS_MATERIAL_EMBEDDED=1)
endif()
