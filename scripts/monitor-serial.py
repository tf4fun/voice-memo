#!/usr/bin/env python3
"""Read the device console and print it line by line.

The USB Serial/JTAG console is the only channel this environment can observe, so
verification logs are collected here instead of through `idf.py monitor` (which
expects an interactive terminal).

Usage:
    python3 scripts/monitor-serial.py /dev/cu.usbmodem14301 [seconds] [--no-reset]
"""

import sys
import time

import serial


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    port = sys.argv[1]
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    reset = "--no-reset" not in sys.argv

    deadline = time.time() + duration
    with serial.Serial(port, 115200, timeout=0.5) as conn:
        # The ESP32-C3 USB Serial/JTAG console ignores baud rate, but pyserial
        # needs a sane value. Pulsing RTS/DTR resets the chip so boot logs are
        # captured instead of missed.
        if reset:
            conn.setDTR(False)
            conn.setRTS(True)
            time.sleep(0.2)
            conn.setRTS(False)
            time.sleep(0.1)

        buffer = b""
        while time.time() < deadline:
            chunk = conn.read(4096)
            if not chunk:
                continue
            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                sys.stdout.write(line.decode("utf-8", "replace").rstrip() + "\n")
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
