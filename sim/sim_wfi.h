/* Included ahead of the generated copy of kernel/kernel.c: its WFI
 * waits for the next tick instead of doing nothing */
#pragma once
void sim_wfi(void);
#define HOST_WFI() sim_wfi()
