/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/*  Serial I/O interface for TTY devices. Simplifies open/read/write code. */

/* Copyright (C) Harry Bock 2006, 2007
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
 
#ifndef _SERIALIO_H
#define _SERIALIO_H

#include "rigel-defs.h"

#if defined(linux) || defined(CYGWIN)
    #define DEFAULT_SERIAL_PORT "/dev/ttyS0"
#elif defined(__APPLE__)
    #define DEFAULT_SERIAL_PORT "/dev/tty.serial"
#else
    #define DEFAULT_SERIAL_PORT "/dev/tts/0"
#endif

#define SERIAL_GRACE_TIMEOUT 5000

void waitus(long us);
     
int sio_valid(int fd);
int sio_open (const char *device);
int sio_close(int fd);
int sio_read (int fd, void *buffer, word maxlen);
int sio_write(int fd, const void *data, word length);
void sio_settimeout(int tout);

#endif /* _SERIALIO_H */
