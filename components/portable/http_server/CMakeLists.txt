# Two transports, one front door.
#
# http_server.c and http_parse.c are the front: the route table, the reply rules,
# the request rules, and the public API - identical on both tiers. http_loop.c is
# the hand-written loop: the host's whole server, and the device's plain listener.
#
# idf/http_server_idf.c is the device's transport and only the firmware compiles
# it: esp_https_server on 443 for the app. The host build picks
# http_backend_single.c instead (see host/Makefile). The backend is chosen by
# which file the build compiles; no shared source branches on the target. The
# device's file lives in a subdirectory because it is the one part of this
# component that cannot compile without ESP-IDF, and host/tests/layering.sh's H3
# check is about the portable sources.
idf_component_register(SRCS "http_server.c" "http_parse.c" "http_loop.c"
                            "idf/http_server_idf.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_timer esp_https_server tls_material)
