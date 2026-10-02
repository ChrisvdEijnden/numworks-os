#pragma once
#include <stdint.h>
#include <stdbool.h>
#define HOST_NAK     (-1)
#define HOST_STALL   (-2)
#define HOST_TIMEOUT (-3)
extern bool sim_vbus;
extern int sim_errors, sim_reg_writes;
void host_bus_reset(void);
void host_set_address(int a);
void host_addr_check(bool on);
bool host_connected(void);
int  host_setup(const uint8_t s[8]);
int  host_in(int ep, uint8_t *buf);
int  host_out(int ep, const uint8_t *buf, int n);
int  sim_in_toggle(int ep);
int  sim_out_toggle(int ep);
uint32_t sim_dcfg(void);
void sim_session_end(void);
