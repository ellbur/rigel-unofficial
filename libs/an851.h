/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* librigel API for communicating with a PIC16/PIC18F device using the AN851 
 * protocol over a TTY device (i.e., RS-232). */

/* Copyright (C) Harry Bock 2006, 2007
 * 
 * This program is free software; you can redistribute it and/or modify it 
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#ifndef _AN851_H
#define _AN851_H

#include "pic18.h"
#include "rigel-defs.h"

#include <sys/types.h>

/* Control characters */
#define DLE 0x05                /* Data Link Escape */
#define ETX 0x04                /* End of TeXt */
#define STX 0x0F                /* Start of TeXt */

/* PIC commands */
#define RD_VERSION 0x00
#define RD_FLASH   0x01
#define WR_FLASH   0x02
#define ER_FLASH   0x03
#define RD_EEDATA  0x04
#define WR_EEDATA  0x05
#define RD_CONFIG  0x06
#define WR_CONFIG  0x07
#define IFI_RUN_CODE 0x08
#define IFI_WR_ROW   0x09
#define PIC_RESET    0xFF /* This can really be any number */

/* Maximum size of the AN851 Receive/Transmit Buffer */
#define MAX_PACKET_SIZE 255
#define MAX_DATA_LENGTH 250

#define AN851_MINOR_VER(x) LOBYTE(x)
#define AN851_MAJOR_VER(x) HIBYTE(x)

/* Break down a full 24-bit address into bytes for requests requiring
 * an address (most of them) */
#define ADDRL(d) ((byte)(d & 0xFF));
#define ADDRH(d) ((byte)((d >> 8) & 0xFF));
#define ADDRU(d) ((byte)((d >> 16) & 0xFF));

/* Use this macro to construct a 21-bit address from the 3 bytes
 * received from the PIC during read operations (ADDRL, ADDRH, ADDRU) */
#define ADDRESS(l,h,u) (((uint32_t)l        & 0x0000FF) | \
                       (((uint32_t)h << 8)  & 0x00FF00) | \
                       (((uint32_t)u << 16) & 0xFF0000) )

/* Use this macro to find out if x is a control character for AN851 packets */
#define IS_CONTROL(x) (x == DLE || \
                       x == ETX || \
                       x == STX)

struct an851_packet {
    byte command;
    uint8_t length, request_length;
    uint8_t data[MAX_PACKET_SIZE];
    uint8_t checksum;
};

struct an851_config {
   int fd;
   int wlag, rlag, reset_lag;
   
   struct pic18_memory_layout mem;
   
   uint8_t  max_data_length;
   uint8_t  lastcmd;
   uint32_t lastaddr;
}; 

/* AN851 Bootloader Protocol [AN851, Appendix A] */
int an851_safe_init(int fd);
int an851_init(int fd, int wlag, int rlag, struct pic18_memory_layout mmap);
int an851_wait_response(uint8_t *buf, size_t max);

int an851_reset  (void);
int an851_version(void);
int ifi_run_program(void);
int ifi_wr_row(uint32_t address, uint8_t rows, uint8_t val);

int an851_rd(uint8_t command, uint32_t address, uint8_t length, void *rx);

int an851_rd_flash (uint32_t address, uint8_t length, void *flashdata);
int an851_rd_eeprom(uint16_t address, uint8_t length, void *eedata);
int an851_rd_config(uint32_t address, uint8_t length, void *configdata);

int an851_wr_flash (uint32_t address, uint8_t blocks, void *data);
int an851_wr_eeprom(uint16_t address, uint8_t length, void *data);
int an851_wr_config(uint8_t confaddr, uint8_t length, void *data);

int an851_er_flash(uint32_t address, uint8_t rows);

int an851_repeat(void);
int an851_replicate_write(uint8_t write_command, uint8_t length, uint32_t address);

#endif /* _AN851_H */
