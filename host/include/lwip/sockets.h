#pragma once
/*
 * Host stand-in for lwip/sockets.h. lwIP follows BSD sockets, and so does
 * macOS, so the real system headers are what the components should see -
 * including SO_RCVTIMEO/SO_SNDTIMEO taking a struct timeval, as the code
 * assumes. Listed explicitly rather than pulling in the whole world.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
