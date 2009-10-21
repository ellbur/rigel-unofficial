/* -*- mode: C; indent-tabs-mode: nil; -*- */

/*  Abstracted routines for in-circuit programming of a PIC18F device using the
 *  AN851 bootloader protocol, including extensions in the implementation by
 *  IFI Robotics */

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

#ifndef _DEVICE_H
#define _DEVICE_H

#include "rigel-defs.h"
#include "an851.h"
#include "pic18.h"

#define DEVICE_NAME_LEN 16
#define DEVICE_BUFFER_SIZE 256


#define VALID_FLASH(m, a) \
     ((a) <= (m).flash_high)
    
/* All functions in this library return -1 on failure and 0 on 
 * success. Any function can fail if a serial communication error
 * occurs. This library is NOT re-entrant. */
 
/* Structure representing everything associated with a device:
 * dev_id: uint16_t value sent from device identifying chip
 * dev_name: product name associated with dev_id (PIC18F8722 etc)
 * bootver: AN851 version reported
 *
 * mem: addresses representing memory bounds for specific device 
 * functions (EEPROM, program memory, etc)
 *
 * config: configuration registers. automatically read on 
 *     device_connect, access as device->config.C4L for CONFIG4L,
 *     for example.
 *
 * opts: connection file descriptor and options set by the program
 *     (verify data [eeprom/flash] on write, maximum data to be sent
 *     for a packet)
 *
 * status_callback: optional function pointer to a routine to be used
 *     whenever real-time data is needed from the device_x functions
 *     (i.e., when writing status bars) while they read/write data.
 */
 
typedef void (*DeviceUpdateCallback)(uint32_t current,
                                     uint32_t total);

typedef struct device {
    uint16_t dev_id, bootver;
    char dev_name[DEVICE_NAME_LEN];
    
    struct pic18_memory_layout mem;
    struct pic18_config_registers config;
    
    struct __opts {
        int fd;
        uint8_t verify_on_write;
        uint8_t max_packet_size;
        int rlag, wlag;
    } opts;

    struct __dev_state {
        uint8_t connected;
        int tx_count, rx_count;
        int refcount_allocs;
    } state;
    
    int is_ifi;
    unsigned char buffer[DEVICE_BUFFER_SIZE];
    DeviceUpdateCallback update_func;
} device_t;

/* device_connect - Connect to the device, which must be in bootloader mode.
 * @tty: full path name of serial device node (i.e., /dev/ttyS0)
 * @dev: device structure to be filled in on successful connect
 * @list: list of possible device IDs and their corresponding memory maps
 * @numdev: number of devices in @list */
int device_connect(const char *tty,
                   struct device *dev,
                   struct device **list,
                   int numdev);

int
device_connect_only(const char *tty,
                    struct device *dev);
    
int device_disconnect(struct device *dev);

/* Reset the device (bringing it back into bootloader mode) */    
void device_reset(const struct device *dev);

/* Run the current program on the device - you can't get back into
 * bootloader mode after making this call. */
void device_run_program(const struct device *dev);

/* Get the device ID (2 bytes); returns a signed long integer, which
 * should be checked for failure (-1) and cast to uint16_t if successful. */
long device_get_id(struct device *dev);

/* Most of the functions in this library can report the status of their
 * operations (which may be very long in the case of program loading or
 * reading) through the use of a callback function set by the user.
 * If cb is not NULL, on each pass of the operation (every write, etc)
 * cb is called with the current address of the operation and the final
 * address, which can be used to implement a progress bar or similar cue. */
void device_set_callback(struct device *dev,
                         DeviceUpdateCallback cb);

/* Erase any number of rows (64 bytes) from the flash memory of the device. */
int device_erase_flash(const struct device *dev,
                       uint32_t address,
                       uint16_t rows);

/* Read or write flash memory to/from a buffer at a specific
 * address. Fails if a call tries to access an invalid region
 * of memory. */
int device_read_flash(const struct device *dev,
                      uint32_t address,
                      uint32_t length,
                      void *data);

int device_write_flash(struct device *dev,
                       uint32_t address,
                       uint32_t length,
                       void *data);


int device_load_program(struct device *dev,
                        void *mem,
                        uint32_t start,
                        uint32_t end);

/* EEPROM operations. */
int device_write_eeprom(struct device *dev,
                        uint32_t address,
                        uint16_t length,
                        void *data);

int device_read_eeprom(const struct device *dev,
                       uint32_t address,
                       uint16_t length,
                       void *buffer);

#endif /* _DEVICE_H */
