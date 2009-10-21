/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* A few useful functions on top of the librigel API for use with the CLI program.
 * Handles config files, memory reading to file, and a basic implementation
 * of the device update callback in the librigel API. */

/*  Copyright (C) Harry Bock 2006, 2007 <hbock@providence.edu>
 *  
 *  This program is free software; you can redistribute it and/or modify it 
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

#ifndef _RIGEL_COMMON_H
#define _RIGEL_COMMON_H
    
#include "rigel-defs.h"

#include "device.h"
#include "inhex32.h"
#include "serialio.h"

#define LOADER_MAJOR_VERSION 1
#define LOADER_MINOR_VERSION 0

#define CONFIG_MAX_DEVICES 32

typedef enum {
    USER_PROC_FLASH = 1,
    USER_EEPROM_DATA,
    USER_BOOT_SECTOR
} frc_mem_region;

typedef struct rigel_options {
   char *device, *config, *file, *fterm, *etc;
   byte dump;    /* Dump a region of memory to file (program, EEPROM, etc.) */
   byte dumpall; /* Force complete dump (no checking for end of prog. mem) */
   byte conf;    /* Output useful configuration register settings. */
   byte run;     /* Run program before disconnect (default true) */
   byte term;    /* Read output of TTY port immediately upon running. */
   byte verify;  /* Verify write operations. */
   byte fmt;     /* Dump to binary file instead of HEX */
   byte eeprom;  /* Load a binary file to EEPROM. */
   byte erase;   /* Erase the device. */
   byte master;  /* Perform operations on IFI master processor */
   byte noifi;   /* Disable IFI extensions */
   byte help;    /* Display short usage or full help */
   byte interrupt;
} rigel_t;

/* Parse the rigelrc file at the path fn and fill in the
 * corresponding devlist. */
int rigel_rc_parse(const char *fn,
                   struct device **devlist);
     
void rigel_rc_free(struct device **);

int rigel_memdump(struct device *dev,
                  const char *fn,
                  int region,
                  int format,
                  uint32_t *size);

void load_update(dword, dword);

/* Load a program or binary file into memory from any of the following
 * formats: INHEX32, IFI BIN, raw data. Returns a newly allocated block
 * of memory containing the program data specified in the arg fn.
 * Arguments start and end will be filled with the starting and ending
 * addresses of the data in the file fn.
 * Must be freed with rigel_program_free. */
void *rigel_program_alloc(const struct device *dev, const char *fn, int format,
                          uint32_t *start, uint32_t *end);
void rigel_program_free(const struct device *dev, void *mem);

/* Reads the entire programmable region of flash memory to buffer, stopping
 * when it reads max_packet_size bytes of erased memory. */
int rigel_read_user(const struct device *dev, void *buffer, size_t bufsiz);

/* Reads the boot sector (anywhere from 1KB-4KB) of the
 * device flash memory into buffer. */
int rigel_read_loader(const struct device *dev, void *buffer, size_t bufsiz);

#endif /* _RIGEL_COMMON_H */
