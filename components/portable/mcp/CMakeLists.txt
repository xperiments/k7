idf_component_register(SRCS "mcp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES library mini_icecast http_server snapshot)

# Control tools are opt-in at build time. The environment variable keeps the
# default firmware byte-for-byte free of them.
if(DEFINED ENV{XTAPE_MCP_CONTROL} AND "$ENV{XTAPE_MCP_CONTROL}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XTAPE_MCP_CONTROL=1)
    message(STATUS "xtape: MCP control tools enabled")
else()
    message(STATUS "xtape: MCP control tools disabled")
endif()
