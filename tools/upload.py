#!/usr/bin/env python3
"""
NumWorks OS — PC File Transfer Tool
Usage:
  python upload.py --port COM3 upload hello.py
  python upload.py --port /dev/ttyACM0 download hello.py
  python upload.py --port /dev/ttyACM0 list
  python upload.py --port /dev/ttyACM0 delete hello.py
"""
import serial, sys, time, argparse, os

def open_port(port, baud=115200):
    return serial.Serial(port, baud, timeout=2)

def cmd_list(s):
    s.write(b"LIST\r\n")
    time.sleep(0.2)
    print(s.read(s.in_waiting).decode(errors='replace'))

def cmd_upload(s, filename):
    data = open(filename, 'rb').read()
    name = os.path.basename(filename)
    hdr  = f"SEND {name} {len(data)}\r\n".encode()
    s.write(hdr)
    time.sleep(0.05)
    s.write(data)
    time.sleep(0.1)
    resp = s.read(s.in_waiting)
    print(f"Uploaded {name} ({len(data)} bytes):", resp.decode(errors='replace'))

def cmd_download(s, filename):
    s.write(f"RECV {filename}\r\n".encode())
    time.sleep(0.1)
    hdr = s.readline().decode().strip()
    if hdr.startswith("DATA "):
        size = int(hdr.split()[1])
        data = b""
        while len(data) < size:
            chunk = s.read(min(256, size - len(data)))
            if not chunk: break
            data += chunk
        open(filename, 'wb').write(data)
        print(f"Downloaded {filename} ({len(data)} bytes)")
    else:
        print("Error:", hdr)

def cmd_delete(s, filename):
    s.write(f"DEL {filename}\r\n".encode())
    time.sleep(0.1)
    print(s.read(s.in_waiting).decode(errors='replace'))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--port', default='/dev/ttyACM0')
    p.add_argument('command', choices=['list','upload','download','delete'])
    p.add_argument('filename', nargs='?')
    args = p.parse_args()

    with open_port(args.port) as s:
        if args.command == 'list':   cmd_list(s)
        elif args.command == 'upload':   cmd_upload(s, args.filename)
        elif args.command == 'download': cmd_download(s, args.filename)
        elif args.command == 'delete':   cmd_delete(s, args.filename)
