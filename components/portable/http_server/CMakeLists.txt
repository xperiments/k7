# One transport. http_server.c and http_parse.c are the front: the route table, the
# reply rules, the request rules and the public API. http_loop.c is the hand-written
# loop that serves it - on the device and on the host, the same file, because the
# device speaks plain HTTP only (no certificate, no trust step, no secure context
# to lose). http_backend_single.c is the host's single-listener binding of it.
idf_component_register(SRCS "http_server.c" "http_parse.c" "http_loop.c"
                            "http_backend_single.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_timer)
