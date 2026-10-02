#!/usr/bin/env python3
"""Run the internal-flash loader (build/loader.bin) in a Cortex-M7
emulator (Unicorn), with models of the parts it touches: RCC, GPIO
ports A-E (and key 6 of the keyboard matrix), the QUADSPI controller
and the AT25SF641 flash behind it, ST's bootloader in system memory.

The flash model is strict where a real chip would misbehave: a command
while it is in continuous-read mode or deep power-down, a command too
soon after leaving deep power-down, a write to status register 2 that
would change bits other than QE, a quad read without the QE bit, a read
of the QSPI window before memory-mapped mode is set up.

Each scenario ends when the loader jumps to the OS's reset handler or
to ST's bootloader; the state left behind is checked against what the
OS (or the bootloader) expects.

Usage: loader_emu.py <loader.bin> [<os image .bin>]
"""
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_READ
import unicorn.arm_const as A

sys.path.insert(0, __import__('os').path.dirname(__file__))
FLASH, RAM, SYSMEM, QWIN = 0x08000000, 0x20000000, 0x1FF00000, 0x90000000
RCC, GPIO0, QSPI, SCS, DWT = 0x40023800, 0x40020000, 0xA0001000, 0xE000E000, 0xE0001000
ROM_SP, ROM_PC = 0x20003FF0, 0x1FF00401
LQ_CCR_QUAD = 0x1F0E3DEB    # checked against loader_qspi.h below

GPIO_RESET = {  # MODER, OTYPER, OSPEEDR, PUPDR (RM0431 §6.4)
    0: (0xA8000000, 0, 0x0C000000, 0x64000000),
    1: (0x00000280, 0, 0x000000C0, 0x00000100),
}


def bits(v, sh, n):
    return (v >> sh) & ((1 << n) - 1)


class Violation(Exception):
    pass


class Flash:
    """AT25SF641 (datasheet DS-25SF641-111C), as far as the loader uses it"""

    def __init__(self, image, qe=True, cont=False, dpd=False, busy=0,
                 qe_writable=True, dead=False, sr2_other=0x00):
        self.mem = bytearray(image) + bytearray(b'\xff' * (8 * 1024 * 1024 - len(image)))
        self.qe, self.cont, self.dpd, self.busy = qe, cont, dpd, busy
        self.qe_writable, self.dead, self.sr2_other = qe_writable, dead, sr2_other
        self.wel = False
        self.nv_writes = 0
        self.released_at = None   # instruction count when deep power-down ended
        self.errors = []
        self.log = []

    def sr2(self):
        return self.sr2_other | (0x02 if self.qe else 0)

    def err(self, msg):
        self.errors.append(msg)

    def run(self, t, now):
        """One transaction (dict of the CCR fields etc.); returns data read"""
        n = t['n']
        floating = [0xFF] * n
        if self.dead:
            return floating
        if self.dpd:
            if t['imode'] == 1 and t['instr'] == 0xAB:
                self.dpd = False
                self.released_at = now
                self.log.append('release')
            return floating                          # everything else ignored
        if self.cont:
            if t['imode'] != 0:
                self.err('instruction 0x%02X sent while the flash is in continuous-read mode' % t['instr'])
                return floating
            if not (t['admode'] == 3 and t['abmode'] == 3 and t['absize'] == 0 and t['dcyc'] == 4 and t['dmode'] == 3):
                self.err('continuous-mode read with wrong lines or dummy cycles')
                return floating
            if (t['abr'] & 0xF0) != 0xA0:
                self.cont = False
                self.log.append('leave-continuous')
            return list(self.mem[t['addr']:t['addr'] + n])
        if t['imode'] == 0:
            # Normal mode: IO0 during address and mode bits is read as an
            # instruction. Only all-zero (00h, not a command) is harmless.
            if t['admode'] == 3 and t['addr'] == 0 and t['abmode'] == 3 and t['abr'] == 0:
                self.log.append('noop-00')
                return floating
            self.err('instruction-less transaction in normal mode would be misread as a command')
            return floating
        if t['imode'] != 1:
            self.err('instruction on %d lines, flash expects 1' % t['imode'])
            return floating
        ins = t['instr']
        if self.released_at is not None and now - self.released_at < 48:
            self.err('command 0x%02X only %d instructions after release from deep power-down (tRES1 3 us)'
                     % (ins, now - self.released_at))
        self.released_at = None
        if self.busy and ins not in (0x05, 0x35):
            if ins == 0xAB:               # ignored; harmless: a busy chip isn't in deep power-down
                self.log.append('release-ignored-busy')
                return floating
            self.err('command 0x%02X while busy' % ins)
            return floating
        single_data = t['dmode'] == 1 and t['admode'] == 0
        if ins == 0x05:
            if not single_data:
                self.err('RDSR1 with wrong lines')
            v = (1 if self.busy else 0) | (2 if self.wel else 0)
            if self.busy:
                self.busy -= 1
            return [v] * n
        if ins == 0x35:
            if not single_data:
                self.err('RDSR2 with wrong lines')
            return [self.sr2()] * n
        if ins == 0x9F:
            return [0x1F, 0x32, 0x17][:n] + [0xFF] * max(0, n - 3)
        if ins == 0xAB:
            return floating
        if ins == 0x06:
            self.wel = True
            return []
        if ins == 0x31:
            if not self.wel:
                self.err('WRSR2 without write enable')
                return []
            if n != 1 or t['dmode'] != 1:
                self.err('WRSR2 with wrong data')
            v = t['wdata'][0]
            if (v & ~0x02) != self.sr2_other:
                self.err('WRSR2 0x%02X changes bits other than QE (was 0x%02X)' % (v, self.sr2()))
            self.nv_writes += 1
            if self.qe_writable:
                self.qe = bool(v & 0x02)
            self.wel = False
            self.busy = 3
            return []
        if ins == 0xEB:
            if not self.qe:
                self.err('Fast Read Quad I/O without the QE bit')
                return floating
            if not (t['admode'] == 3 and t['abmode'] == 3 and t['absize'] == 0 and t['dcyc'] == 4 and t['dmode'] == 3):
                self.err('Fast Read Quad I/O with wrong lines or dummy cycles')
                return floating
            if (t['abr'] & 0xF0) == 0xA0:
                self.cont = True
            return list(self.mem[t['addr']:t['addr'] + n])
        if ins == 0x0B:
            if not (t['admode'] == 1 and t['abmode'] == 0 and t['dcyc'] == 8 and t['dmode'] == 1):
                self.err('Fast Read with wrong lines or dummy cycles')
                return floating
            return list(self.mem[t['addr']:t['addr'] + n])
        self.err('unexpected command 0x%02X' % ins)
        return floating


class Board:
    def __init__(self, loader, image, key6=False, **flash):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
        self.flash = Flash(image, **flash)
        self.key6 = key6
        self.errors = []
        self.icount = 0
        self.min_sp = 1 << 32
        self.result = None
        self.window_reads = 0
        self.vtor = 0
        # memory
        uc.mem_map(FLASH, 0x10000)
        uc.mem_write(FLASH, loader)
        uc.mem_map(RAM, 0x40000)
        uc.mem_map(SYSMEM, 0x10000)
        uc.mem_write(SYSMEM, struct.pack('<II', ROM_SP, ROM_PC))
        uc.mem_map(QWIN, 8 * 1024 * 1024)
        uc.mem_write(QWIN, bytes(self.flash.mem))
        uc.mmio_map(RCC, 0x400, self.rcc_rd, None, self.rcc_wr, None)
        uc.mmio_map(GPIO0, 0x1400, self.gpio_rd, None, self.gpio_wr, None)
        uc.mmio_map(QSPI, 0x400, self.q_rd, None, self.q_wr, None)
        uc.mmio_map(SCS, 0x1000, self.scs_rd, None, self.scs_wr, None)
        uc.mmio_map(DWT, 0x1000, self.dwt_rd, None, self.dwt_wr, None)
        self.demcr = 0
        self.dwt_on, self.dwt_base, self.dwt_unlocked, self.dwt_frozen = False, 0, False, 0
        uc.hook_add(UC_HOOK_CODE, self.on_code)
        uc.hook_add(UC_HOOK_MEM_READ, self.on_window_read, begin=QWIN, end=QWIN + 8 * 1024 * 1024 - 1)
        # peripherals
        self.rcc = {0x10: 0, 0x18: 0, 0x30: 0x00100000, 0x38: 0}
        self.gpio = {}
        for p in range(5):
            self.reset_port(p)
        self.reset_qspi()
        sp, pc = struct.unpack('<II', loader[:8])
        self.os_entry = struct.unpack('<I', image[4:8])[0] & ~1 if len(image) >= 8 else None
        uc.reg_write(A.UC_ARM_REG_MSP, sp)
        uc.reg_write(A.UC_ARM_REG_SP, sp)
        self.entry = pc

    def err(self, msg):
        if msg not in self.errors:
            self.errors.append(msg)

    # ── RCC ──
    def rcc_rd(self, uc, off, size, ud):
        return self.rcc.get(off, 0)

    def rcc_wr(self, uc, off, size, value, ud):
        old = self.rcc.get(off, 0)
        self.rcc[off] = value
        if off == 0x10:          # AHB1RSTR: a released reset bit resets the port
            for p in range(5):
                if old & (1 << p) and not value & (1 << p):
                    self.reset_port(p)
        if off == 0x18 and old & 2 and not value & 2:
            self.reset_qspi()

    def clock_on(self, port):
        return bool(self.rcc[0x30] & (1 << port))

    # ── GPIO ──
    def reset_port(self, p):
        m, ot, sp, pu = GPIO_RESET.get(p, (0, 0, 0, 0))
        self.gpio[p] = {0x00: m, 0x04: ot, 0x08: sp, 0x0C: pu, 0x14: 0, 0x20: 0, 0x24: 0}

    def pin_mode(self, p, n):
        return bits(self.gpio[p][0x00], 2 * n, 2)

    def idr(self, p):
        g, v = self.gpio[p], 0
        for n in range(16):
            mode = self.pin_mode(p, n)
            if mode == 1:
                level = bits(g[0x14], n, 1)
            else:
                pull = bits(g[0x0C], 2 * n, 2)
                level = 1 if pull == 1 else 0       # floating reads 0
                if p == 2 and n == 2 and self.key6:  # key 6: column 3 (PC2) to row G (PA6)
                    if self.pin_mode(0, 6) == 1 and not bits(self.gpio[0][0x14], 6, 1):
                        level = 0
            v |= level << n
        return v

    def gpio_rd(self, uc, off, size, ud):
        p, r = off // 0x400, off % 0x400
        if not self.clock_on(p):
            self.err('GPIO%s read without its clock' % 'ABCDE'[p])
            return 0
        if r == 0x10:
            return self.idr(p)
        return self.gpio[p].get(r, 0)

    def gpio_wr(self, uc, off, size, value, ud):
        p, r = off // 0x400, off % 0x400
        if not self.clock_on(p):
            self.err('GPIO%s written without its clock' % 'ABCDE'[p])
            return
        g = self.gpio[p]
        if r == 0x18:             # BSRR
            g[0x14] = (g[0x14] | (value & 0xFFFF)) & ~(value >> 16)
        else:
            g[r] = value

    # ── QUADSPI ──
    def reset_qspi(self):
        self.q = {'CR': 0, 'DCR': 0, 'DLR': 0, 'CCR': 0, 'AR': 0, 'ABR': 0}
        self.tcf = False
        self.fifo = []
        self.wpending = None      # indirect write waiting for data
        self.mapped = False
        self.mapped_busy = False
        self.mm_first = True
        self.final_regs = None
        self.last_indirect_read = False   # ES0360: abort needed before mapping

    def ccr_fields(self, ccr):
        return dict(instr=ccr & 0xFF, imode=bits(ccr, 8, 2), admode=bits(ccr, 10, 2),
                    adsize=bits(ccr, 12, 2), abmode=bits(ccr, 14, 2), absize=bits(ccr, 16, 2),
                    dcyc=bits(ccr, 18, 5), dmode=bits(ccr, 24, 2), fmode=bits(ccr, 26, 2),
                    sioo=bits(ccr, 28, 1))

    def busy(self):
        return self.wpending is not None or bool(self.fifo) or self.mapped_busy

    def transaction(self, addr=0, n=None, wdata=None):
        t = self.ccr_fields(self.q['CCR'])
        t['abr'] = self.q['ABR']
        t['addr'] = addr
        t['n'] = (self.q['DLR'] + 1) if (n is None and t['dmode']) else (n or 0)
        t['wdata'] = wdata or []
        if t['admode'] and t['adsize'] != 2:
            self.err('address size is not 24 bits')
        return t

    def begin(self):
        t = self.transaction(self.q['AR'])
        if t['fmode'] == 0 and t['dmode']:
            self.wpending = (t, [])
            return
        data = self.flash.run(t, self.icount)
        if t['fmode'] == 1:
            self.fifo = list(data)
        self.tcf = True

    def q_rd(self, uc, off, size, ud):
        if not self.rcc[0x38] & 2:
            self.err('QUADSPI read without its clock')
            return 0
        if off == 0x08:           # SR
            sr = 0
            if self.tcf:
                sr |= 2
            fm = bits(self.q['CCR'], 26, 2)
            if (fm == 1 and self.fifo) or (fm == 0 and self.wpending is not None):
                sr |= 4
            if self.busy():
                sr |= 0x20
            sr |= min(len(self.fifo), 31) << 8
            return sr
        if off == 0x20:           # DR
            if not self.fifo:
                self.err('DR read with an empty FIFO')
                return 0
            v = 0
            for i in range(size):
                if self.fifo:
                    v |= self.fifo.pop(0) << (8 * i)
            return v
        names = {0x00: 'CR', 0x04: 'DCR', 0x10: 'DLR', 0x14: 'CCR', 0x18: 'AR', 0x1C: 'ABR'}
        return self.q.get(names.get(off, ''), 0)

    def q_wr(self, uc, off, size, value, ud):
        if not self.rcc[0x38] & 2:
            self.err('QUADSPI written without its clock')
            return
        if off == 0x00:           # CR
            if value & 2:         # ABORT
                self.last_indirect_read = False
                self.mapped = self.mapped_busy = False
                self.fifo, self.wpending = [], None
                value &= ~2
            elif self.busy() and (value ^ self.q['CR']) & ~1:
                self.err('CR changed while busy')
            self.q['CR'] = value
            return
        if off == 0x0C:           # FCR
            if value & 2:
                self.tcf = False
            return
        if off == 0x20:           # DR (indirect write data)
            if self.wpending is None:
                self.err('DR written with no write in progress')
                return
            t, buf = self.wpending
            buf += [(value >> (8 * i)) & 0xFF for i in range(size)]
            if len(buf) >= t['n']:
                t['wdata'] = buf[:t['n']]
                self.wpending = None
                self.flash.run(t, self.icount)
                self.tcf = True
            return
        name = {0x04: 'DCR', 0x10: 'DLR', 0x14: 'CCR', 0x18: 'AR', 0x1C: 'ABR'}.get(off)
        if name is None:
            self.err('write to QUADSPI offset 0x%X' % off)
            return
        if self.busy():
            self.err('%s written while busy' % name)
        self.q[name] = value
        if not self.q['CR'] & 1 and name in ('CCR', 'AR'):
            self.err('command started with QUADSPI disabled')
        if name == 'CCR':
            fm = bits(value, 26, 2)
            if fm == 3:
                if self.last_indirect_read:
                    self.err('memory-mapped mode entered right after an indirect read without abort (ST errata ES0360)')
                self.mapped, self.mm_first = True, True
            elif fm == 1:
                self.last_indirect_read = True
                if bits(value, 10, 2) == 0:
                    self.begin()
            elif fm == 0 and bits(value, 10, 2) == 0:
                self.last_indirect_read = False
                self.begin()
        elif name == 'AR' and not self.mapped and bits(self.q['CCR'], 10, 2):
            self.begin()

    def on_window_read(self, uc, access, addr, size, value, ud):
        self.window_reads += 1
        if not (self.mapped and self.q['CR'] & 1):
            self.err('QSPI window read before memory-mapped mode')
            return
        ccr = self.q['CCR']
        n = 0x10000 if False else size
        t = self.transaction(addr - QWIN, size)
        if t['sioo'] and not self.mm_first:
            t['imode'] = 0          # instruction only on the first access
        self.mm_first = False
        self.mapped_busy = True
        before = len(self.flash.errors)
        data = self.flash.run(t, self.icount)
        if len(self.flash.errors) == before and bytes(data) != bytes(self.flash.mem[addr - QWIN:addr - QWIN + size]):
            self.err('memory-mapped read returned wrong data')

    # ── SCB, DWT cycle counter (one cycle per instruction: a lower bound) ──
    def scs_rd(self, uc, off, size, ud):
        return {0xD08: self.vtor, 0xDFC: self.demcr}.get(off, 0)

    def scs_wr(self, uc, off, size, value, ud):
        if off == 0xD08:
            self.vtor = value
        elif off == 0xDFC:
            self.demcr = value

    def dwt_rd(self, uc, off, size, ud):
        if off == 0x004:
            return (self.icount - self.dwt_base) & 0xFFFFFFFF if self.dwt_on else self.dwt_frozen
        if off == 0x000:
            return 1 if self.dwt_on else 0
        return 0

    def dwt_wr(self, uc, off, size, value, ud):
        if off == 0xFB0:
            self.dwt_unlocked = value == 0xC5ACCE55
            return
        if not (self.demcr & (1 << 24)) or not self.dwt_unlocked:
            self.err('DWT written while disabled or locked')
            return
        if off == 0x004:
            self.dwt_base = self.icount - value
            self.dwt_frozen = value
        elif off == 0x000:
            if value & 1 and not self.dwt_on:
                self.dwt_base = self.icount - getattr(self, 'dwt_frozen', 0)
            self.dwt_on = bool(value & 1)

    # ── CPU ──
    def on_code(self, uc, addr, size, ud):
        self.icount += 1
        if self.icount % 64 == 0:
            self.min_sp = min(self.min_sp, uc.reg_read(A.UC_ARM_REG_SP))
        if addr == ROM_PC & ~1:
            self.result = 'rom'
            uc.emu_stop()
        elif QWIN <= addr < QWIN + 0x800000:
            self.result = 'os' if addr == self.os_entry else 'os-wrong-entry@%08X' % addr
            self.final_regs = dict(self.q)
            uc.emu_stop()
        elif not (FLASH <= addr < FLASH + 0x10000):
            self.result = 'stray@%08X' % addr
            uc.emu_stop()

    def run(self, budget=30_000_000):
        try:
            self.uc.emu_start(self.entry, FLASH + 0x10000, count=budget)
        except UcError as e:
            self.result = 'crash: %s at %08X' % (e, self.uc.reg_read(A.UC_ARM_REG_PC))
        if self.result is None:
            self.result = 'hang (no jump after %d instructions)' % self.icount
        self.sp = self.uc.reg_read(A.UC_ARM_REG_SP)
        return self


fails = 0


def check(cond, what):
    global fails
    print('  %s %s' % ('ok  ' if cond else 'FAIL', what))
    if not cond:
        fails += 1


def boot(loader, image, **kw):
    b = Board(loader, image, **kw).run()
    return b


def clean(b):
    errs = b.errors + b.flash.errors
    if errs:
        for e in errs:
            print('       !', e)
    return not errs


def qspi_pins_ok(b):
    want = {(1, 2): 9, (1, 6): 10, (2, 9): 9, (3, 12): 9, (4, 2): 9, (3, 13): 9}
    for (p, n), af in want.items():
        g = b.gpio[p]
        if b.pin_mode(p, n) != 2 or bits(g[0x20 + 4 * (n >> 3)], 4 * (n & 7), 4) != af:
            return False
        if bits(g[0x08], 2 * n, 2) != 3 or bits(g[0x0C], 2 * n, 2) != 0:
            return False
    return True


def main():
    loader = open(sys.argv[1], 'rb').read()
    real = open(sys.argv[2], 'rb').read() if len(sys.argv) > 2 else None
    synth = struct.pack('<II', 0x20004000, 0x90000201) + bytes(range(256)) * 64
    image = real or synth
    hdr = open(__import__('os').path.join(__import__('os').path.dirname(__file__), '../../loader/loader_qspi.h')).read()
    global LQ_CCR_QUAD

    print('normal start:')
    b = boot(loader, image)
    check(b.result == 'os', 'jumps to the OS reset handler (%s)' % ('the real OS image' if real else 'a test image'))
    check(clean(b), 'no protocol errors, no QSPI window reads before mapping')
    sp = struct.unpack('<I', image[:4])[0]
    check(b.sp == sp and b.vtor == QWIN, 'stack pointer from the OS vector table, VTOR = 0x90000000')
    f = b.final_regs or {}
    ccr = f.get('CCR', 0)
    check(ccr & 0xFF == 0xEB and bits(ccr, 8, 2) == 1 and bits(ccr, 10, 2) == 3 and bits(ccr, 24, 2) == 3
          and bits(ccr, 18, 5) == 4 and bits(ccr, 26, 2) == 3 and bits(ccr, 28, 1) == 1 and f.get('ABR') == 0xA0,
          'memory-mapped Fast Read Quad I/O (EBh, 1-4-4, 4 dummy, mode A0h, SIOO)')
    cr, dcr = f.get('CR', 0), f.get('DCR', 0)
    check(bits(cr, 24, 8) == 1 and cr & 1 and bits(dcr, 16, 5) == 22 and bits(dcr, 8, 3) == 4 and dcr & 1,
          'clock /2, 8 MB, chip select high 5 cycles (52 ns at 96 MHz), clock mode 3')
    check(b.flash.cont, 'the flash is in continuous-read mode, as the OS storage driver expects')
    check(qspi_pins_ok(b), 'QSPI pins: PB2 PC9 PD12 PD13 PE2 on AF9, PB6 on AF10, very high speed, no pull')
    check(b.flash.nv_writes == 0, 'QE already set: no status register write')
    check(b.window_reads == 2, 'reads only the 2 vector words from the QSPI window before jumping')
    check(b.gpio[0][0x00] == GPIO_RESET[0][0] and b.gpio[2][0x0C] == 0, 'keyboard pins back in their reset state')
    check(0x20010000 - b.min_sp < 256, 'stack use under 256 bytes (%d)' % (0x20010000 - b.min_sp))
    print('       (%d instructions)' % b.icount)

    print('flash left in a strange state by a reset:')
    b = boot(loader, image, cont=True)
    check(b.result == 'os' and clean(b) and 'leave-continuous' in b.flash.log, 'continuous-read mode: left first, then boots')
    b = boot(loader, image, dpd=True)
    check(b.result == 'os' and clean(b) and 'release' in b.flash.log, 'deep power-down: released (waiting tRES1), then boots')
    b = boot(loader, image, busy=40)
    check(b.result == 'os' and clean(b), 'an erase still running: waits for it, then boots')

    print('quad enable bit:')
    b = boot(loader, image, qe=False, sr2_other=0x40)
    check(b.result == 'os' and clean(b) and b.flash.qe and b.flash.nv_writes == 1, 'QE off: set once (other bits kept), quad reads')
    b = boot(loader, image, qe=False, qe_writable=False)
    ccr = (b.final_regs or {}).get('CCR', 0)
    check(b.result == 'os' and clean(b) and ccr & 0xFF == 0x0B and bits(ccr, 24, 2) == 1 and bits(ccr, 18, 5) == 8,
          "QE can't be set: falls back to Fast Read (0Bh) on one line, 8 dummy")

    print('recovery (ST bootloader):')
    b = boot(loader, image, key6=True)
    check(b.result == 'rom' and clean(b), 'key 6 held: jumps to ST\'s bootloader')
    check(b.vtor == SYSMEM and b.sp == ROM_SP, "with its vector table and stack pointer")
    check(b.window_reads == 0 and not (b.rcc[0x38] & 2), 'without touching the QSPI flash (QUADSPI clock off)')
    check(b.gpio[0][0x00] == GPIO_RESET[0][0] and b.gpio[2][0x00] == 0 and b.pin_mode(1, 4) == 1 and bits(b.gpio[1][0x14], 4, 1),
          'ports back in reset state, red LED (PB4) on')
    b = boot(loader, b'\xff' * 64)
    check(b.result == 'rom' and clean(b), 'empty QSPI flash: ST\'s bootloader instead of a crash')
    b = boot(loader, struct.pack('<II', 0x20004000, 0x90000200) + bytes(64))
    check(b.result == 'rom', 'reset vector without the Thumb bit: refused')
    b = boot(loader, struct.pack('<II', 0x30000000, 0x90000201) + bytes(64))
    check(b.result == 'rom', 'stack pointer outside RAM: refused')
    b = boot(loader, struct.pack('<II', 0x20004000, 0x907C0101) + bytes(64))
    check(b.result == 'rom', 'reset handler in the file system area: refused')
    b = boot(loader, image, dead=True)
    check(b.result == 'rom' and b.icount < 200000, "flash not answering (reads all 1s): ST's bootloader at once")
    if len(sys.argv) > 3:
        quick = open(sys.argv[3], 'rb').read()   # built with a 5 ms busy timeout
        b = boot(quick, image, busy=10**9)
        check(b.result == 'rom' and clean(b), 'flash busy for ever: gives up after the timeout (test build: 5 ms)')
        b = boot(quick, image, busy=40)
        check(b.result == 'os' and clean(b), '... but waits out a shorter erase')
    b = boot(loader, image)
    check(b.demcr == 0 and not b.dwt_on, 'cycle counter switched off again before the jump')

    print('%s (%d failure%s)' % ('FAILED' if fails else 'ALL PASSED', fails, '' if fails == 1 else 's'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
