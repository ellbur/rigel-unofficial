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
#define _POSIX_C_SOURCE 199309

#include "serialio.h"

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

static struct timeval ttytimeout;    
static fd_set ttyfds;

/* TODO: Consider using setitimer for this function. */
void
waitus(long us)
{
    struct timespec wtime;
    
    /* This allows us to specify a microsecond value that is
     * more than the equivalent to 1 second */
    wtime.tv_sec  = us / 1000000;
    wtime.tv_nsec = (us % 1000000) * 1000;

    if(nanosleep(&wtime, &wtime) == -1 && errno == EINTR )
        rigel_error("nanosleep interrupted with %ld nanoseconds remaining.\n",
		    wtime.tv_nsec);
}

int
sio_valid(int fd)
{   
    struct stat fdstat;

    /* FIXME: check errno */
    if(fstat(fd, &fdstat) == -1)
        return -1;

    if(!S_ISCHR(fdstat.st_mode)) {
        rigel_error("file descriptor is not a valid character device.");
        return -1;
    }
    
    return 0;
}

int
sio_read(int fd, void *buffer, word maxlen)
{
    int ret;
    ssize_t rx;

    rx = 0;    
    FD_ZERO(&ttyfds);
    FD_SET(fd, &ttyfds);
    
    waitus(5000);

    /* I'd prefer poll(2) but it is not implemented on OS X */
    ret = select(fd+1, &ttyfds, NULL, NULL, &ttytimeout);
    if(!ret) return 0;   /* Timeout */
    else if(ret == -1) { /* Error (interrupted, bad fd) */
        rigel_error("read select failed: %s\n", strerror(errno));
        return -1;
    }
    
    if(FD_ISSET(fd, &ttyfds)) {
        rx = read(fd, buffer, maxlen);
        
        //if(rx == -1) rigel_error("Cannot read from serial port! (%s)\n", strerror(errno));
    }
        
    return rx;
}

int
sio_write(int fd, const void * data, word length)
{
    ssize_t written, len;

    len = 0;
    
    do {
        written = write(fd, data, length);
        if(written < 0) {
             if(errno == EINTR)
                  continue;
             return -1;
        }
        len += written;
        data += written;
        length -= written;
    } while(written > 0);
    
    return (int)len;
}

/* This function sets up a total timeout (in ms) for any device read, plus
 * 5ms grace time for our device to actually receive and process the data. */
void
sio_settimeout(int tout)
{
    ttytimeout.tv_sec  = tout / 1000;
    ttytimeout.tv_usec = ((tout % 1000) * 1000);
}

int
sio_open(const char *device)
{    
    int fd;
    struct termios tty_opts;

    fd = open(device, O_RDWR | O_NOCTTY);
    if(fd == -1) { /* Usually a permissions error. */
        rigel_error("TTY open failed: %s\n", strerror(errno));
        return -1;
    }        
    
    /* Setting up serial communication interface:
	 * Do nothing for all possible control chars in termios.c_cc.
	 * According to the AN851 specification:
	 *  - 8 data bits (CS8)
	 *  - No parity check (IGNPAR)
	 *  - 1 STOP bit (default)
	 *  - 115200 default baud; */
    memset(tty_opts.c_cc, 0, NCCS);
    
    tty_opts.c_iflag = IGNPAR;
    tty_opts.c_oflag = 0;
    tty_opts.c_lflag = 0;
    tty_opts.c_cflag = B115200 | CLOCAL | CREAD | CS8;

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &tty_opts);
    
    sio_settimeout(0); /* Immediate initial timeout */
    
    return fd;
}

int
sio_close(int fd)
{
    return close(fd);
}
