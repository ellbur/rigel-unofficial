#ifndef __AN851D_H
#define __AN851D_H

#include <rigel-defs.h>
#include <an851.h>

#define AN851D_VERSION 0x1439
#define AN851D_DEVID   0x1420 /* PIC18F8722 */

#define INTERNAL_BUFFER_SIZE 255

#define DEVID_ADDR 0x3FFFFE

int an851d_reset  (void);
int an851d_version(void);

int an851d_rd_flash (dword address, byte length);
int an851d_rd_eeprom(dword address, byte length);
int an851d_rd_config(dword address, byte length);

int an851d_wr_flash (dword address, byte blocks, void *data);
int an851d_wr_eeprom(word address,  byte length, void *data);
int an851d_wr_config(byte confaddr, byte length, void *data);

int an851d_er_flash(dword address, byte rows);

int an851d_repeat(void);
int an851d_replicate_write(byte write_command, byte length, dword address);

#endif
