/* Copyright (C) Harry Bock 2006-2008 <hbock@providence.edu>
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

#include "device.h"

#include "an851.h"
#include "serialio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int device_is_ifi(const struct device *dev);

int
device_connect_only(const char *tty, struct device *dev)
{
    register int fd;
    if((fd = sio_open(tty)) == -1 || sio_valid(fd) == -1)
        return -1;
    
    return fd;
}

int
device_connect(const char *tty, struct device *dev,
               struct device **list, int numdev)
{
    int i, fd, bver, found;
    found = 0;
    
    if((fd = device_connect_only(tty, dev)) == -1)
        return -1;
    
    /* We perform a "safe" initialization of the AN851
     * interface (very generous read/write times) so we
     * can write a short identification command and then
     * do a proper configuration optimized for our device. */
    an851_safe_init(fd);
    
    if( (bver = an851_version()) == -1 ) {
        rigel_error("Is your device connected to %s and in program mode?\n", tty);
        return -1;
    }
                   
    if(device_get_id(dev) == -1) {
        rigel_error("reading device ID\n");
        return -1;
    }
    
    for(i = 0; i < numdev; i++)
        if(dev->dev_id & list[i]->dev_id) {
            memcpy(dev, list[i], sizeof(struct device));            
            found = 1;
            break;
        }
    dev->bootver = (uint16_t)bver;
    dev->opts.fd = fd;
    dev->state.connected = 0;
    
    if(!found) {
        rigel_error("unknown device! DEVID registers: %04X\n", dev->dev_id);
        return -1;
    }
    
    an851_init(dev->opts.fd, dev->opts.wlag, dev->opts.rlag, dev->mem);
    
    if((dev->is_ifi = device_is_ifi(dev)) == -1)
        return -1;
        
    if(an851_rd_config(dev->mem.config_low, 
                       sizeof(struct pic18_config_registers),
                       &dev->config) == -1) {
        rigel_error("Could not read configuration registers!\n");
        return -1;
    }
    
    /* The BBSIZ bits of the CONFIG4L register describe the
     * boot configuration of the device. For all IFI controllers (AFAIK),
     * this is 2KB (BBSIZ == 00b). */
    if((dev->config.C4L & BBSIZ) != 0x00) {
        if((dev->config.C4L & BBSIZ) == 0x10)
             dev->mem.flash_low = 0x1000; /* BBSIZ = 01b, 4KB */
        else dev->mem.flash_low = 0x2000; /* BBSIZ = 10b/11b, 8KB */
    }   else dev->mem.flash_low = 0x0800; /* BBSIZ = 00b, 2KB */

    dev->state.connected = 1;
    return dev->dev_id;
}

/* If we do not get a response from the device with a IFI_WR_ROW
 * command, the device is not using IFI's bootloader. This is necessary
 * to perform a proper bootloader exit. */
static int
device_is_ifi(const struct device *dev)
{
    uint8_t temp[BYTES_PER_ROW];

    /* Save first row of data and attempt to use the IFI_WR_ROW
     * command - if it works, we have an IFI device, and we must
     * re-write that data back. */
    if(an851_rd_flash(dev->mem.flash_low, BYTES_PER_ROW, temp) == -1)
        return -1;
    
    if(ifi_wr_row(dev->mem.flash_low, 1, 0x00) == 0) {
        an851_wr_flash(dev->mem.flash_low,
                       BYTES_PER_ROW / BYTES_PER_BLOCK, temp);
        return 1;
    }
    
    return 0;
}

int
device_disconnect(struct device *dev)
{
    if(dev->state.connected)
        sio_close(dev->opts.fd);
    dev->state.connected = 0;

    if(dev->state.refcount_allocs != 0)
        rigel_warn("Device %s memory refcount is %d, should be 0!\n",
             dev->dev_name, dev->state.refcount_allocs);

    return 0;
}

void
device_run_program(const struct device *dev)
{
    static uint8_t run = 0x57;

    if(!dev->state.connected)
        return;
    
    if(dev->is_ifi)
        ifi_run_program();
    else {
        /* For non-IFI devices using the AN851 bootloader, to
         * leave boot mode you must write a non-0xFF value to
         * the last byte in data EEPROM. */
        an851_wr_eeprom(dev->mem.eeprom_high, 1, &run);
        an851_reset(); /* Is this necessary? */
    }
}

void
device_reset(const struct device *dev)
{
    if(dev->state.connected)
        an851_reset();
}

void
device_set_callback(struct device *dev, DeviceUpdateCallback cb)
{
    dev->update_func = cb;
}

/* Read the PIC device ID, which is at "program memory" addresses
 * 0x3FFFFE and 0x3FFFFF. */
long
device_get_id(struct device *dev)
{
    uint8_t devid[2];

    if(an851_rd(RD_FLASH, 0x3FFFFE, 2, devid) == -1)
        return -1;
        
    dev->dev_id = MAKEWORD(devid[0], devid[1]);
    return (long)dev->dev_id;
}

int
device_erase_flash(const struct device *dev, uint32_t address, uint16_t rows)
{
    uint8_t max = 0xFF; /* We can erase up to 255 rows at a time */
    uint32_t erased;

    if(!dev->state.connected)
        return -1;

    if(address < dev->mem.flash_low ||
      (address + rows * BYTES_PER_ROW) > dev->mem.flash_high + 1) {
        rigel_error("Cannot erase invalid flash region!\n");
        return -1;
    }

    if(max > rows)
        max = rows;
    
    /* Erase operations are performed on ROWS, not bytes. One row
     * is 64 bytes in most cases; change BYTES_PER_ROW if not. */
    for(erased = 0; erased < rows; erased += max) {
        if(rows - erased < max)
            max = rows - erased;
        
        if(dev->is_ifi) {
            if(ifi_wr_row(address + (erased * BYTES_PER_ROW), max, 0x00) == -1)
                return -1;
        } else {
            if(an851_er_flash(address + (erased * BYTES_PER_ROW), max) == -1)
                return -1;
        }
        
        if(dev->update_func)
            dev->update_func(erased, rows);        
    }
    if(dev->update_func)
        dev->update_func(rows, rows);
         
    return 0;
}

int
device_write_eeprom(struct device *dev, uint32_t address,
                    uint16_t length, void *udata)
{
    int c;
    uint8_t *data = (uint8_t*)udata;
    uint8_t max = dev->opts.max_packet_size;

    if(!dev->state.connected)
        return -1;
    
    if((address+length) > dev->mem.eeprom_high) {
        rigel_error("cannot write to invalid EEPROM address!\n");
        return -1;
    }
            
    for(c = 0; c < length; c += max) {
        /* If there isn't enough data left to fill the maximum
         * buffer, adjust the final amount of data to be written
         * to prevent overflow */
        if((length - c) < max)
            max = (length - c);
        an851_wr_eeprom(address+c, max, &data[c]);
        
        if(dev->update_func)
            dev->update_func(c, length);
        
        if(dev->opts.verify_on_write) {
            if( an851_rd_eeprom(address+c, max, dev->buffer) == -1 ||
                memcmp(&data[c], dev->buffer, max) != 0 ) {
                rigel_error("Error verifying EEPROM write, "
                      "address %04Xh!\n", address+c);
                return -1;
            }
        }
    }
    if(dev->update_func)
        dev->update_func(length, length);
    
    return 0;
}

int
device_read_eeprom(const struct device *dev, uint32_t address,
                   uint16_t length, void *data)
{
    uint16_t c;
    uint8_t max = dev->opts.max_packet_size;

    if(!dev->state.connected)
        return -1;
    
    if( (address+length-1) > dev->mem.eeprom_high ) {
        rigel_error("cannot read invalid EEPROM address!\n");
        return -1;
    }
    
    for(c = 0; c < length; c += max) {
        if((length - c) < max)
            max = (length - c);
        
        if( an851_rd_eeprom(address+c, max, data+c) == -1 ) {
            rigel_error("Could not read EEPROM data from %08X-%08X.\n", address+c, address+c+max);
            return -1;
        }
        
        if(dev->update_func)
            dev->update_func(c, length);
    }
    if(dev->update_func)
        dev->update_func(length, length);

    return 0;
}

int
device_read_flash(const struct device *dev, uint32_t address, uint32_t length, void *buffer)
{
    uint32_t cur = 0;
    uint8_t max = dev->opts.max_packet_size;

    if(!dev->state.connected)
        return -1;
    
    if( (address+length-1) > dev->mem.flash_high ) {
        rigel_error("cannot read invalid flash address!\n");
        return -1;
    }
    
    while(cur < length) {
        if((length - cur) < max)
            max = (length - cur);
            
        if( an851_rd_flash(address+cur, max, buffer+cur) == -1 ) {
            rigel_error("reading flash memory\n");
            return -1;
        }
        
        cur += max;
        if(dev->update_func)
            dev->update_func(cur, length);
    }
    return 0;
}

int
device_write_flash(struct device *dev, uint32_t address, uint32_t length, void *udata)
{
    uint32_t i;
    uint8_t *memory = (uint8_t *)udata;
    uint8_t max = dev->opts.max_packet_size / BYTES_PER_BLOCK;
    uint32_t nbytes = max * BYTES_PER_BLOCK;
    
    /* The device's flash memory can hold flash_high+1 bytes of memory.
     * Writing to flash memory is a block operation, so convert this
     * to blocks [8 bytes per block on PIC18F microcontrollers] */
    uint32_t blocks = length / BYTES_PER_BLOCK,
             end    = address + length;

    if(!dev->state.connected)
        return -1;
        
    if(!VALID_FLASH(dev->mem, address) || 
       !VALID_FLASH(dev->mem, address + length - 1)) {
        rigel_error("cannot write to invalid flash address!\n");
        return -1;
    }
        
    /* Writing max_packet_size bytes at a time, in blocks */
    for(i = 0; (i < blocks) && (address < end); i += max) {
    
        /* Adjust blocks to write if there are less than max_packet_size
         * bytes to be written */
        if(blocks - i < max) {
            max = blocks - i;
            nbytes = max * BYTES_PER_BLOCK;
        }
        
        /* Utilize the device "scratch" buffer to enforce 8-byte alignment.
         * The most data allowed to be written at any time is 250 bytes,
         * so by using dev->buffer, it's alway aligned. */
        memset(dev->buffer, 0xFF, DEVICE_BUFFER_SIZE);
        memcpy(dev->buffer, &memory[address], nbytes);
        if(an851_wr_flash(address, max, dev->buffer) == -1) {
            rigel_error("writing flash memory\n");
            return -1;
        }
        
        if(dev->update_func)
            dev->update_func(i, blocks);
        
        /* If user wants to verify what has been written (very good idea!)
         * read the block(s) we just wrote and compare it to what is in
         * the HEX file */
        if(dev->opts.verify_on_write) {
            if(an851_rd_flash(address, nbytes, dev->buffer)  == -1 ||
               memcmp(&memory[address], dev->buffer, nbytes) != 0) {
                rigel_error("verifying flash write, address %06Xh!\n", address);
                return -1;
            }
        }
        
        address += max * BYTES_PER_BLOCK;
    }
        
    return 0;
}

int
device_load_program(struct device *dev, void *mem, uint32_t start, uint32_t end)
{
    uint32_t psize;
        
    /* Assure we're loading a program that's not too big for our device. */
    if(!VALID_FLASH(dev->mem, end)) {
        rigel_error("device_load_program: program will not fit on device!\n");
        return -1;
    }
    if(start < dev->mem.flash_low && !(dev->config.C6H & WRTB))
        rigel_warn("Program file specifies write address in write-protected "
             "boot sector.\n");

    psize = end - start + 1;
        
    return device_write_flash(dev, start, psize, mem);
}
