import os, subprocess, sys, tempfile, time
# usage: cdc_e2e.py PATH/TO/cdc_bridge  (the device side, on a pseudo-terminal)
TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools')
bridge = subprocess.Popen([os.path.abspath(sys.argv[1])], stdout=subprocess.PIPE, text=True)
port = bridge.stdout.readline().strip()
fails = 0
def run(*args, tool='upload.py', cwd=None):
    if tool == 'upload.py':
        cmd = [sys.executable, os.path.join(TOOLS, tool), '--port', port, *args]
    else:
        cmd = [sys.executable, os.path.join(TOOLS, tool), '--port', port, *args]
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, timeout=30)
def check(cond, what, detail=''):
    global fails
    print(f"  {'ok  ' if cond else 'FAIL'} {what}" + (f"  [{detail.strip()}]" if not cond and detail else ''))
    fails += not cond
work = tempfile.mkdtemp()
try:
    data = bytes((i * 37 + 11) % 256 for i in range(5000))        # binary, includes \r \n and 0x00
    big = bytes((i * 7 + (i >> 8)) % 256 for i in range(100 * 1024))   # exactly 100 KB, the limit
    for name, content in [('bin.dat', data), ('max.bin', big), ('hello.py', b"print('hi')\n")]:
        with open(os.path.join(work, name), 'wb') as f: f.write(content)
    r = run('upload', os.path.join(work, 'bin.dat')); check(r.returncode == 0, 'upload 5000-byte binary file', r.stderr)
    r = run('upload', os.path.join(work, 'max.bin')); check(r.returncode == 0, 'upload a 100 KB file (the limit), streamed to flash', r.stderr)
    with open(os.path.join(work, 'toobig.bin'), 'wb') as f: f.write(b'x' * (100 * 1024 + 1))
    r = run('upload', os.path.join(work, 'toobig.bin')); check(r.returncode == 1 and 'too large' in r.stderr, 'upload of 100 KB + 1 byte refused', r.stderr)
    r = run('upload', os.path.join(work, 'hello.py'), 'script.py', tool='transfer.py'); check(r.returncode == 0, 'transfer.py upload under another name', r.stderr)
    r = run('list'); check(r.returncode == 0 and 'bin.dat' in r.stdout and '5000' in r.stdout and 'script.py' in r.stdout, 'list shows all files with sizes', r.stdout + r.stderr)
    dl = tempfile.mkdtemp()
    r = run('download', 'bin.dat', cwd=dl)
    got = open(os.path.join(dl, 'bin.dat'), 'rb').read() if r.returncode == 0 else b''
    check(got == data, 'download returns the exact 5000 bytes (was cut at 512)', r.stderr)
    r = run('download', 'max.bin', tool='transfer.py', cwd=dl)
    got = open(os.path.join(dl, 'max.bin'), 'rb').read() if r.returncode == 0 else b''
    check(got == big, 'transfer.py download of the 100 KB file, identical', r.stderr)
    r = run('delete', 'bin.dat'); check(r.returncode == 0, 'delete existing file', r.stderr)
    r = run('delete', 'bin.dat'); check(r.returncode == 1 and 'not_found' in r.stderr, 'delete missing file reports ERR (was always OK)', r.stderr)
    r = run('download', 'nope.txt'); check(r.returncode == 1 and 'not_found' in r.stderr, 'download missing file reports ERR', r.stderr)
    r = run('list'); check('bin.dat' not in r.stdout and 'max.bin' in r.stdout, 'list after delete', r.stdout)
    big2 = bytes(reversed(big))
    with open(os.path.join(work, 'max.bin'), 'wb') as f: f.write(big2)
    r = run('upload', os.path.join(work, 'max.bin')); check(r.returncode == 0, 'replace the 100 KB file with another (both copies do not fit: old one goes first)', r.stderr)
    r = run('download', 'max.bin', cwd=dl)
    got = open(os.path.join(dl, 'max.bin'), 'rb').read() if r.returncode == 0 else b''
    check(got == big2, 'the replacement reads back identical', r.stderr)
    with open(os.path.join(work, 'other.bin'), 'wb') as f: f.write(big)
    r = run('upload', os.path.join(work, 'other.bin')); check(r.returncode == 1 and 'no_space' in r.stderr, 'a second 100 KB file: ERR no_space, nothing broken', r.stderr)
    r = run('list'); check(r.returncode == 0 and 'max.bin' in r.stdout and 'other.bin' not in r.stdout, 'list still fine afterwards', r.stdout + r.stderr)
finally:
    bridge.kill()
print(f"{'ALL PASSED' if not fails else 'FAILED'} ({fails} failure{'s' if fails != 1 else ''})")
sys.exit(1 if fails else 0)
