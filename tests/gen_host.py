#!/usr/bin/env python3
"""Make host copies of OS sources that touch the hardware directly.

    gen_host.py ROOT OUT

ROOT is the repository, OUT the directory for the generated files.
Each copy swaps register accesses and ARM assembly for calls or
variables the tests provide, then checks that nothing hardware-specific
is left. Quoted includes are rewritten to absolute paths so the copies
compile from anywhere; the tests add -I for their own fake_hw headers.
"""
import os
import re
import sys

ROOT, OUT = os.path.abspath(sys.argv[1]), sys.argv[2]
os.makedirs(OUT, exist_ok=True)


def read(rel):
    with open(os.path.join(ROOT, rel)) as f:
        return f.read()


def replace(s, old, new, src):
    if old not in s:
        sys.exit(f'gen_host: {src} changed, "{old.strip()[:60]}" not found')
    return s.replace(old, new)


def absolute_includes(s, src):
    base = os.path.dirname(os.path.join(ROOT, src))
    def fix(m):
        if m.group(1).startswith('fake_hw'):        # the test's own header
            return m.group(0)
        return '#include "%s"' % os.path.normpath(os.path.join(base, m.group(1)))
    return re.sub(r'#include "([^"]*)"', fix, s)


def drop_includes(s, names, src):
    for n in names:
        s = replace(s, '#include "%s"\n' % n, '', src)
    return s


def write(name, s, src, forbidden):
    for f in forbidden:
        if f in s:
            sys.exit(f'gen_host: {src}: "{f}" is left in the host copy')
    with open(os.path.join(OUT, name), 'w') as f:
        f.write(s)


# ── hal/fault.c: SCB registers, barriers and the reset request ─────────
src = 'hal/fault.c'
s = read(src)
s = drop_includes(s, ['fault.h', 'hal.h', 'uart.h', 'display.h', 'keyboard.h'], src)
for reg, addr in [('AIRCR', 'ED0C'), ('CFSR', 'ED28'), ('HFSR', 'ED2C'), ('MMFAR', 'ED34'), ('BFAR', 'ED38')]:
    s = re.sub(r'(#define SCB_%s\s+)\(\*\(volatile uint32_t \*\)0xE000%sUL\)' % (reg, addr),
               r'\1host_' + reg.lower(), s)
s = replace(s, 'SCB_AIRCR = (0x5FAUL << 16) | (1U << 2);     /* SYSRESETREQ */', 'host_reset();', src)
s = re.sub(r'__asm volatile\("[a-z ]*" ::: "memory"\);', 'host_barrier();', s)
s = replace(s, 'extern uint32_t _sstack[], _estack[];\n', '', src)
s = replace(s, 'extern volatile uint32_t g_tick_ms;\n', '', src)
s = replace(s, '(uintptr_t)__builtin_return_address(0)', '0x9000ABCDu', src)
write('fault_host.c', absolute_includes(s, src), src, ['asm', '0xE000'])

# ── kernel/kernel.c: interrupt masking and WFI ─────────────────────────
src = 'kernel/kernel.c'
s = read(src)
s = re.sub(r'__asm volatile\("[a-z ]*" ::: "memory"\);', '', s)
s = replace(s, '__asm volatile("wfi");', 'HOST_WFI();', src)
s = ('/* WFI: nothing in the tests; the simulator waits for the next tick */\n'
     '#ifndef HOST_WFI\n#define HOST_WFI() ((void)0)\n#endif\n') + s
write('kernel_host.c', absolute_includes(s, src), src, ['asm'])

# ── hal/display.c: the FMC bus to the panel ─────────────────────────────
src = 'hal/display.c'
s = read(src)
s = drop_includes(s, ['hal.h', 'uart.h', '../include/stm32f730.h'], src)
s = '#include "fake_hw.h"\n' + absolute_includes(s, src)
s = replace(s, '#define LCD_CMD  (*(volatile uint16_t *)0x60000000UL)\n', '', src)
s = replace(s, '#define LCD_DATA (*(volatile uint16_t *)0x60020000UL)\n', '', src)
s = s.replace('(void)LCD_DATA', '(void)host_read()').replace('(uint8_t)LCD_DATA', '(uint8_t)host_read()')
s = re.sub(r'LCD_CMD = (.*?);', r'host_cmd(\1);', s)
s = re.sub(r'LCD_DATA = (.*?);', r'host_data(\1);', s)
for reg in ['BCR1', 'BTR1', 'BWTR1']:
    s = re.sub(r'(#define FMC_%s\s+)\(\*\(volatile uint32_t \*\)0xA000[0-9A-F]{4}UL\)' % reg,
               r'\1fake_' + reg.lower(), s)
s = replace(s, 'return (GPIO_TypeDef *)(AHB1_BASE + 0x400UL * n);', 'return &fake_ports[n];', src)
# the linker script's section for the framebuffer; macOS takes no ELF section names
s = replace(s, ' __attribute__((section(".framebuf")))', '', src)
write('display_host.c', s, src, ['LCD_CMD', 'LCD_DATA', '0x60020000UL', '0xA000', 'AHB1_BASE', 'asm', 'section('])

# ── hal/backlight.c, led.c, battery.c: registers via fake_hw2.h ────────
for name in ['backlight', 'led', 'battery']:
    src = 'hal/%s.c' % name
    s = read(src)
    s = replace(s, '#include "../include/stm32f730.h"', '#include "fake_hw2.h"', src)
    s = s.replace('#include "hal.h"\n', '')
    s = s.replace('__asm volatile("mrs %0, primask\\n cpsid i" : "=r"(primask) :: "memory");', 'primask = 0;')
    s = s.replace('__asm volatile("msr primask, %0" :: "r"(primask) : "memory");', '(void)primask;')
    if name == 'battery':
        s = replace(s, '#define ADC1       ((ADC_TypeDef *)(APB2_BASE + 0x2000UL))',
                    'extern ADC_TypeDef fake_adc;\n#define ADC1 (&fake_adc)', src)
        s = replace(s, '#define ADC_CCR    (*(volatile uint32_t *)(APB2_BASE + 0x2304UL))',
                    'extern uint32_t fake_adc_ccr;\n#define ADC_CCR fake_adc_ccr', src)
        s = replace(s, '#define PCB_VERSION_OTP (*(volatile const uint32_t *)0x1FF07800UL)',
                    'extern uint32_t fake_otp;\n#define PCB_VERSION_OTP fake_otp', src)
        s = replace(s, '    ADC1->SR = 0;\n', '    ADC1->SR = 2;   /* host: the conversion is done at once */\n', src)
    if name == 'led':
        # let the test see every charge state the battery code asks for
        s = replace(s, 'void led_set_charge(led_charge_t state) {', 'void led_set_charge_real(led_charge_t state) {', src)
        s += ('extern led_charge_t last_charge_hook(led_charge_t);\n'
              'void led_set_charge(led_charge_t state) { last_charge_hook(state); led_set_charge_real(state); }\n')
    s = absolute_includes(s, src)
    write('%s_host.c' % name, s, src, ['asm', 'APB2_BASE + 0x2', '0x1FF07800'])
