#!/usr/bin/env python3
"""
NumWorks OS — Host File Transfer Tool (same protocol as upload.py)

Usage:
    python3 transfer.py list
    python3 transfer.py upload <local_file> [<device_name>]
    python3 transfer.py download <device_name>
    python3 transfer.py delete <device_name>

The calculator's file system is flat (no directories).
Requirements: pip install pyserial
"""
import argparse
import os
import sys

from upload import DeviceError, NWDevice


def main(argv=None):
    parser = argparse.ArgumentParser(description='NumWorks OS file transfer tool')
    parser.add_argument('--port', '-p', help='Serial port (auto-detected if omitted)')
    subs = parser.add_subparsers(dest='command', required=True)
    subs.add_parser('list', help='List files')
    p_up = subs.add_parser('upload', help='Upload file to calculator')
    p_up.add_argument('local', help='Local file path')
    p_up.add_argument('dest', nargs='?', help='Name on the calculator (default: file name)')
    p_dl = subs.add_parser('download', help='Download file from calculator')
    p_dl.add_argument('src', help='Name on the calculator')
    p_del = subs.add_parser('delete', help='Delete file on calculator')
    p_del.add_argument('name')
    args = parser.parse_args(argv)

    try:
        with NWDevice(args.port) as dev:
            if args.command == 'list':
                files = dev.list()
                if not files:
                    print("(empty)")
                for name, size in sorted(files):
                    print(f"{name:<24} {size:>8}")
            elif args.command == 'upload':
                with open(args.local, 'rb') as f:
                    data = f.read()
                dest = args.dest or os.path.basename(args.local)
                dev.upload(dest, data)
                print(f"Uploaded {args.local} -> {dest} ({len(data)} bytes)")
            elif args.command == 'download':
                data = dev.download(args.src)
                out = os.path.basename(args.src)
                with open(out, 'wb') as f:
                    f.write(data)
                print(f"Downloaded {args.src} -> {out} ({len(data)} bytes)")
            elif args.command == 'delete':
                dev.delete(args.name)
                print(f"Deleted {args.name}")
    except (DeviceError, OSError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
