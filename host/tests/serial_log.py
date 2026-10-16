#!/usr/bin/env python3
"""Capture the board's serial console for a fixed number of seconds.

    serial_log.py [-p PORT] [-s SECONDS] [-o FILE] [--no-reset]

Part of the board tier: the device's own log is the evidence for the things the
host cannot show - external memory size and mode, which allocation path each
large buffer took, the card mount result, and the encoder's measured cost.

The reset is the usual two-transistor auto-reset circuit: RTS drives EN, DTR
drives GPIO0. Holding GPIO0 high while pulsing EN starts the application rather
than the ROM bootloader.
"""
import argparse
import sys
import time

import serial

ap = argparse.ArgumentParser()
ap.add_argument("-p", "--port", default="/dev/cu.usbserial-834430")
ap.add_argument("-b", "--baud", type=int, default=115200)
ap.add_argument("-s", "--seconds", type=float, default=8.0)
ap.add_argument("-o", "--out", default=None)
ap.add_argument("--no-reset", action="store_true")
args = ap.parse_args()

port = serial.Serial(args.port, args.baud, timeout=0.2)
try:
    if not args.no_reset:
        port.setDTR(False)  # GPIO0 high: run the application
        port.setRTS(True)   # EN low: hold in reset
        time.sleep(0.15)
        port.setRTS(False)  # EN high: release
        time.sleep(0.05)
        port.reset_input_buffer()

    out = open(args.out, "wb") if args.out else sys.stdout.buffer
    deadline = time.time() + args.seconds
    while time.time() < deadline:
        data = port.read(4096)
        if data:
            out.write(data)
            out.flush()
finally:
    port.close()
