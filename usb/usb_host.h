/* ================================================================
 * NumWorks OS — USB Host Driver (Mass Storage / FAT32)
 * File: usb/usb_host.h
 *
 * Minimal USB OTG_FS host stack for Mass Storage Class (BOT).
 * Mounts FAT32 volumes and provides file access.
 * Note: N0120 VBUS is not sourced by default from the calculator.
 *       Use self-powered drives or an OTG adapter with power.
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

int  usb_host_init(void);
int  usb_host_mounted(void);        /* Returns 1 if drive mounted */
void usb_host_process(void);        /* Call periodically */

/* File operations on USB FAT32 volume */
int  usb_host_ls(char names[][32], int maxn);
int  usb_host_read_file(const char *name, uint8_t *buf,
                        uint32_t maxlen, uint32_t *size_out);

/* Import a file into internal flash FS */
int  usb_host_import(const char *usb_name, const char *dest_name);
