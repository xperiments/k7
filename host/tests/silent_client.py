#!/usr/bin/env python3
"""T4 helper: open a TCP connection, send nothing at all, and hold it open.

A server that parses requests on its accept loop blocks here for its whole
receive timeout, which is the defect (D1) that the extraction fixes. The shell
test connects this first and then times a real request.
"""
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

s = socket.create_connection((host, port))
time.sleep(seconds)
try:
    s.close()
except OSError:
    pass
