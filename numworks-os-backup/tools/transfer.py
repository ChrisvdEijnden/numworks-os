#!/usr/bin/env python3
"""
NumWorks OS — Host File Transfer Tool
Communicates with the calculator over USB CDC (virtual serial port)

Usage:
    python3 transfer.py list [path]
    python3 transfer.py upload <local_file> <device_path>
    python3 transfer.py download <device_path>
    python3 transfer.py delete <device_path>
    python3 transfer.py mkdir <device_path>
    python3 transfer.py repl               (interactive Python REPL)

Requirements:
    pip install pyserial
"""

import sys
import os
import time
import argparse
import serial
import serial.tools.list_ports

BAUD_RATE    = 115200
TIMEOUT      = 3.0
NUMWORKS_VID = 0x0483
NUMWORKS_PID = 0x5720


def find_device():
    """Auto-detect NumWorks OS USB CDC port."""
    for port in serial.tools.list_ports.comports():
        if port.vid == NUMWORKS_VID and port.pid == NUMWORKS_PID:
            return port.device
    # Fallback: try common port names
    for candidate in ['/dev/ttyACM0', '/dev/ttyACM1',
                      '/dev/tty.usbmodem*', 'COM3', 'COM4']:
        if os.path.exists(candidate):
            return candidate
    return None


class NWDevice:
    def __init__(self, port=None):
        if port is None:
            port = find_device()
        if port is None:
            print("Error: NumWorks OS device not found.", file=sys.stderr)
            print("Make sure the calculator is connected via USB.", file=sys.stderr)
            sys.exit(1)
        self.ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.2)  # let the device settle
        self.ser.reset_input_buffer()

    def _send(self, cmd: str):
        self.ser.write((cmd + '\n').encode())
        self.ser.flush()

    def _readline(self) -> str:
        line = self.ser.readline()
        return line.decode(errors='replace').rstrip('\r\n')

    def _expect_ok(self):
        resp = self._readline()
        if resp != 'OK':
            raise RuntimeError(f"Device error: {resp}")

    def list_dir(self, path='/'):
        self._send(f'LIST {path}')
        entries = []
        while True:
            line = self._readline()
            if line == 'END':
                break
            if line.startswith('ENTRY '):
                parts = line[6:].rsplit(' ', 2)
                name, size, kind = parts[0], int(parts[1]), parts[2]
                entries.append({'name': name, 'size': size,
                                 'is_dir': kind == 'd'})
            elif line.startswith('ERR'):
                raise RuntimeError(line)
        return entries

    def download(self, device_path: str) -> bytes:
        self._send(f'GET {device_path}')
        header = self._readline()
        if not header.startswith('DATA '):
            raise RuntimeError(f"Unexpected response: {header}")
        size = int(header[5:])
        data = b''
        remaining = size
        while remaining > 0:
            chunk = self.ser.read(min(remaining, 4096))
            if not chunk:
                raise RuntimeError("Timeout while reading file data")
            data += chunk
            remaining -= len(chunk)
        return data

    def upload(self, device_path: str, data: bytes):
        self._send(f'PUT {device_path} {len(data)}')
        ok = self._readline()
        if ok != 'OK':
            raise RuntimeError(f"PUT rejected: {ok}")
        self.ser.write(data)
        self.ser.flush()
        self._expect_ok()

    def delete(self, device_path: str):
        self._send(f'DEL {device_path}')
        self._expect_ok()

    def mkdir(self, device_path: str):
        self._send(f'MKDIR {device_path}')
        self._expect_ok()

    def close(self):
        self.ser.close()


# ── CLI ──────────────────────────────────────────────────────

def cmd_list(dev, args):
    path = args.path if args.path else '/'
    entries = dev.list_dir(path)
    if not entries:
        print("(empty)")
        return
    # Print formatted listing
    print(f"{'Name':<24}  {'Size':>8}  {'Type':<4}")
    print('-' * 42)
    for e in sorted(entries, key=lambda x: (not x['is_dir'], x['name'])):
        kind = 'DIR ' if e['is_dir'] else 'FILE'
        size = '-' if e['is_dir'] else str(e['size'])
        name = e['name'] + '/' if e['is_dir'] else e['name']
        print(f"{name:<24}  {size:>8}  {kind}")


def cmd_upload(dev, args):
    local = args.local
    dest  = args.dest
    if not os.path.isfile(local):
        print(f"Error: local file '{local}' not found")
        sys.exit(1)
    with open(local, 'rb') as f:
        data = f.read()
    print(f"Uploading {local} → {dest} ({len(data)} bytes)...")
    dev.upload(dest, data)
    print("Done.")


def cmd_download(dev, args):
    src = args.src
    data = dev.download(src)
    # Save to local file (basename)
    outname = os.path.basename(src)
    with open(outname, 'wb') as f:
        f.write(data)
    print(f"Downloaded {src} → {outname} ({len(data)} bytes)")


def cmd_delete(dev, args):
    dev.delete(args.path)
    print(f"Deleted {args.path}")


def cmd_mkdir(dev, args):
    dev.mkdir(args.path)
    print(f"Created directory {args.path}")


def main():
    parser = argparse.ArgumentParser(
        description='NumWorks OS file transfer tool')
    parser.add_argument('--port', '-p', help='Serial port (auto-detected if omitted)')

    subs = parser.add_subparsers(dest='command')

    p_list = subs.add_parser('list', help='List files')
    p_list.add_argument('path', nargs='?', default='/')

    p_up = subs.add_parser('upload', help='Upload file to calculator')
    p_up.add_argument('local', help='Local file path')
    p_up.add_argument('dest',  help='Device destination path')

    p_dl = subs.add_parser('download', help='Download file from calculator')
    p_dl.add_argument('src', help='Device file path')

    p_del = subs.add_parser('delete', help='Delete file on calculator')
    p_del.add_argument('path')

    p_mk = subs.add_parser('mkdir', help='Create directory on calculator')
    p_mk.add_argument('path')

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        sys.exit(0)

    dev = NWDevice(args.port)
    try:
        dispatch = {
            'list':     cmd_list,
            'upload':   cmd_upload,
            'download': cmd_download,
            'delete':   cmd_delete,
            'mkdir':    cmd_mkdir,
        }
        dispatch[args.command](dev, args)
    finally:
        dev.close()


if __name__ == '__main__':
    main()
