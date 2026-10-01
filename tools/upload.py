#!/usr/bin/env python3
"""
NumWorks OS — PC file transfer over the calculator's USB serial port.

Usage:
  python upload.py --port COM3 upload hello.py
  python upload.py --port /dev/ttyACM0 download hello.py
  python upload.py --port /dev/ttyACM0 list
  python upload.py --port /dev/ttyACM0 delete hello.py

Protocol (see usb/usb_cdc.c): one command per line, replies end in CRLF.
  LIST               -> "<name> <size>" lines, then "OK"
  RECV <name>        -> "DATA <size>", <size> raw bytes, then "OK"
  SEND <name> <size> -> "READY", then the PC sends <size> bytes -> "OK"
  DEL <name>         -> "OK"
  failures           -> "ERR <reason>"

Requires: pip install pyserial
"""
import argparse
import glob
import os
import sys

TIMEOUT = 3.0
MAX_FILE_SIZE = 8 * 1024   # FFS_MAX_FILE_SIZE in include/config.h


class DeviceError(RuntimeError):
    pass


def find_port():
    """First serial port that looks like a USB CDC device, or None."""
    for pattern in ('/dev/ttyACM*', '/dev/tty.usbmodem*', '/dev/cu.usbmodem*'):
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            if p.vid is not None:
                return p.device
    except ImportError:
        pass
    return None


class NWDevice:
    def __init__(self, port=None, timeout=TIMEOUT):
        import serial
        port = port or find_port()
        if not port:
            raise DeviceError("no calculator found; pass --port")
        self.ser = serial.Serial(port, 115200, timeout=timeout)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _send_line(self, line):
        self.ser.write((line + '\r\n').encode())
        self.ser.flush()

    def _read_line(self):
        raw = self.ser.readline()
        if not raw.endswith(b'\n'):
            raise DeviceError("timeout waiting for the calculator")
        line = raw.decode(errors='replace').rstrip('\r\n')
        if line.startswith('ERR'):
            raise DeviceError(line)
        return line

    def _read_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.ser.read(n - len(data))
            if not chunk:
                raise DeviceError("timeout while receiving file data")
            data += chunk
        return data

    def list(self):
        self._send_line('LIST')
        files = []
        while True:
            line = self._read_line()
            if line == 'OK':
                return files
            name, size = line.rsplit(' ', 1)
            files.append((name, int(size)))

    def download(self, name):
        self._send_line('RECV ' + name)
        header = self._read_line()
        if not header.startswith('DATA '):
            raise DeviceError("unexpected reply: " + header)
        data = self._read_exact(int(header[5:]))
        if self._read_line() != 'OK':
            raise DeviceError("transfer not confirmed")
        return data

    def upload(self, name, data):
        if ' ' in name or not name or len(name) > 23:
            raise DeviceError("name must be 1-23 characters without spaces")
        if len(data) > MAX_FILE_SIZE:
            raise DeviceError(f"file too large ({len(data)} > {MAX_FILE_SIZE} bytes)")
        self._send_line(f'SEND {name} {len(data)}')
        if self._read_line() != 'READY':
            raise DeviceError("calculator not ready")
        self.ser.write(data)
        self.ser.flush()
        if self._read_line() != 'OK':
            raise DeviceError("upload not confirmed")

    def delete(self, name):
        self._send_line('DEL ' + name)
        self._read_line()   # "OK" (an ERR raises)


def main(argv=None):
    p = argparse.ArgumentParser(description='NumWorks OS file transfer')
    p.add_argument('--port', help='serial port (auto-detected if omitted)')
    p.add_argument('command', choices=['list', 'upload', 'download', 'delete'])
    p.add_argument('filename', nargs='?')
    args = p.parse_args(argv)
    if args.command != 'list' and not args.filename:
        p.error(f"{args.command} needs a filename")

    try:
        with NWDevice(args.port) as dev:
            if args.command == 'list':
                for name, size in dev.list():
                    print(f"{name:<24} {size:>6} B")
            elif args.command == 'upload':
                with open(args.filename, 'rb') as f:
                    data = f.read()
                name = os.path.basename(args.filename)
                dev.upload(name, data)
                print(f"Uploaded {name} ({len(data)} bytes)")
            elif args.command == 'download':
                data = dev.download(args.filename)
                with open(os.path.basename(args.filename), 'wb') as f:
                    f.write(data)
                print(f"Downloaded {args.filename} ({len(data)} bytes)")
            elif args.command == 'delete':
                dev.delete(args.filename)
                print(f"Deleted {args.filename}")
    except (DeviceError, OSError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
