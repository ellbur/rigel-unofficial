/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* librigel API for reading and writing Intel HEX32 files into memory. Supports
 *  both extended linear and segment addressing. */

/* Copyright (C) Harry Bock 2006, 2007 <hbock@providence.edu>
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

#ifndef _INHEX32_H
#define _INHEX32_H

#include "rigel-defs.h"
#include <sys/types.h>

#define INHEX_MAX_DATA 0x10

#define INHEX_DATA 0x00
#define INHEX_EOF  0x01
#define INHEX_EXT_SEGMENT 0x02
#define INHEX_EXT_LINEAR  0x04

#define IFIBIN_DATA_LEN 0x10
#define IFIBIN_LINE_LEN 0x37

#define CONFIG_REGISTER_MASK 0x300000

#ifdef __ARCH_LE
    #define SET_EXT_LINEAR(addr, ext)  ((addr) |= ((ext) << 16) & 0xFFFF0000)
    #define SET_EXT_SEGMENT(addr, ext) ((addr) += ((ext) << 4)  & 0x000FFFF0)
#else /* FIXME: I'm almost positive this is not correct. */
    #define SET_EXT_LINEAR(addr, ext)  ((addr) |= ((ext) >> 16) & 0x0000FFFF)
    #define SET_EXT_SEGMENT(addr, ext) ((addr) += ((ext) >> 4)  & 0x0FFFF000)
#endif

struct inhex32_record {
    uint8_t  length;
    uint32_t address;
    uint8_t  record_type;
    uint8_t  data[16];
    uint8_t  checksum;
};

enum ProgramAddressMode {
     LiteralAddressMode,
     SegmentAddressMode,
     LinearAddressMode
};

typedef enum {
    IntelHexFormat,
    InnovationFirstFormat,
    BinaryDataFormat
} program_format_t;

typedef int (*FormatReadFunc)(const char *,
                              void *,
                              size_t,
                              uint32_t *,
                              uint32_t *);

int inhex32_write(const char *fn, void *mem, uint32_t start, uint32_t end);
int inhex32_read (const char *fn, void *mem, size_t memsize, 
                  uint32_t *start, uint32_t *end);

int inhex32_validate(const char *fn);

int ifi_bin_write(const char *fn, void *pmem, uint32_t start, uint32_t end);
int ifi_bin_read (const char *fn, void *buffer, size_t bufsize, 
                  uint32_t *start, uint32_t *end);

#endif
