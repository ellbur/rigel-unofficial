/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* A few useful functions on top of the librigel API for use with the CLI program.
 * Handles config files, memory reading to file, and a basic implementation
 * of the device update callback in the librigel API. */

/*  Copyright (C) Harry Bock 2006-2008 <hbock@providence.edu>
 *
 *  This program is free software; you can redistribute it and/or modify 
 *  it under the terms of the GNU General Public License as published by
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
#include "rigel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>

int hash_total = 30;
int hash_current = 0;

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

static int
binaryf_read(const char *fn, void *mem, size_t memsize, uint32_t *start, uint32_t *end);

void *
rigel_program_alloc(const struct device *dev, const char *fn, int format,
		    uint32_t *start, uint32_t *end)
{
    uint8_t *mem;
    FormatReadFunc fmt_read = inhex32_read;

    switch(format) {
    case IntelHexFormat: fmt_read = inhex32_read; break;
    case InnovationFirstFormat: fmt_read = ifi_bin_read; break;
    case BinaryDataFormat:      fmt_read = binaryf_read; break;
    default:
        rigel_error("unsupported data file format\n");
        return NULL;
    }

    if(fmt_read(fn, NULL, dev->mem.flash_high+1, start, end) == -1)
        return NULL;

    *end = PIC18_ALIGN_TO_ROW(*end);
    if(!(mem = (uint8_t*)malloc(*end)))
        return NULL;

    if(fmt_read(fn, mem, *end, NULL, NULL) == -1) {
        free(mem);
        return NULL;
    }

    return mem;
}

void
rigel_program_free(const struct device *dev, void *mem)
{
    if(mem)
        free(mem);
}

/* Read user program memory (does NOT include boot loader!) */
int
rigel_read_user(const struct device *dev, void *buffer, size_t bufsiz)
{
    int all_erased, check = 0, erased = 0;
    uint8_t *data = (uint8_t *)buffer;
    uint8_t  max  = dev->opts.max_packet_size;
    uint32_t high = dev->mem.flash_high,
          low  = dev->mem.flash_low;
    uint32_t addr, c;
    
    /* IFI loaders, for whatever reason, clear all the bits
     * of program memory instead of doing the normal operation
     * of setting them (writing only clears bits). So if we know
     * it is an IFI controller, check for 0x00 instead of 0xFF. */
    uint8_t erase_byte = (dev->is_ifi) ? 0x00 : 0xFF;

    if(!dev->state.connected)
        return -1;
    
    addr = low;
    while(addr < high) {
        if((high - addr) < max)
            max = high - addr;

        if((addr + max) >= bufsiz) {
            rigel_warn("Program size read exceeds allocated buffer! Halting.\n");
            max -= (addr + max - bufsiz);
        }
        
        if(an851_rd_flash(addr, max, &data[addr]) == -1)
            return -1;

        /* If we read all FFs or 00s (erased/IFI "erased") 4 times
         * (512 bytes), it's almost surely the end of the program */
        all_erased = 1;
        for(c = addr; c < (addr + max); c++)
            if(data[c] != erase_byte) {
                all_erased = erased = check = 0;
                break;
            }

        /* Increment the current address now - if we lop off the end
         * of our heuristic before doing so, we lose max bytes of
         * data */
        addr += max;
        
        if(all_erased) {
            check++;
            erased += max; /* Tally bytes we've counted... */
        }
        if(check == 4) {
            if(dev->update_func) dev->update_func(addr, addr);
            addr -= erased; /* ...and don't save them at the end */
            break;
        } else if(dev->update_func) dev->update_func(addr, high);
    }
    
    return addr;        
}

int
rigel_read_loader(const struct device *dev, void *buffer, size_t bufsiz)
{
    /* flash_low is exactly the size needed */
    if(bufsiz < dev->mem.flash_low) {
        rigel_error("Cannot read boot sector, buffer too small\n");
        return -1;
    }

    return device_read_flash(dev, 0, dev->mem.flash_low, buffer);
}

int
rigel_erase_device(const struct device *dev)
{
    return device_erase_flash(dev, dev->mem.flash_low,
			      (dev->mem.flash_high - dev->mem.flash_low + 1) /
			      BYTES_PER_ROW);
}

static int
binaryf_read(const char *fn, void *mem, size_t memsize, 
             uint32_t *start, uint32_t *end)
{
    FILE *fp;
    uint32_t _end;
    
    if(!(fp = fopen(fn, "rb")))
        return -1;
        
    fseek(fp, 0, SEEK_END);

    _end = (uint32_t)ftell(fp);
    if(_end > memsize) {
        rigel_error("File %s will not fit on device!\n", fn);
        return -1;
    }

    if(start && end) {
        *start = 0;
        *end = _end;
    }

    if(mem) {
        rewind(fp);
        fread(mem, 1, _end, fp);
    }
    
    fclose(fp);
    return 0;
}

static FILE *
rigel_get_rc(const char *rigelrc)
{
    size_t rd;
    FILE *fp, *cp;
    char file[256];
    char data[128];
    const char *home = getenv("HOME");
    
    snprintf(file, 256, "%s/.rigelrc", home);
    
    /* First check the user-requested configuration file. */
    if( !rigelrc || !(fp = fopen(rigelrc, "r")) ) {
        /* If the user has their own .rigelrc, use it. */
        if(access(file, F_OK | R_OK) == 0)
            return fopen(file, "r");
            
        /* If this is the first time rigel is run by a local
        * user, copy the global configuration file /etc/rigelrc
        * to $HOME/.rigelrc. */
        else if(access(SYSCONFDIR "/rigelrc", F_OK | R_OK) == 0) {
            printf("First-run: creating user configuration file %s/.rigelrc\n", home);
            fp = fopen(SYSCONFDIR "/rigelrc", "r");
            cp = fopen(file, "w+");

            if(!cp) {
                rigel_error("Cannot create user configuration file %s/.rigelrc!\n"
                            "Defaulting to system configuration file.", home);
                return fp;
            } else {
                /* Copy /etc/rigelrc to $HOME/.rigelrc */
                while(!feof(fp)) {
                    rd = fread(data, 1, 128, fp);
                    fwrite(data, 1, rd, cp);
                }
                fclose(fp);
                rewind(cp);
                
                return cp;
            }
        }
    } else return fp;
         
    /* If there is no local, global, or specific config file, we have
     * a major problem. */
    return NULL;
}

/* Sets up dev as a null-terminated array of struct device structs allocated
 * on the heap - free with rigel_rc_free(struct device **)
 * TODO: Add more rigorous sanity checks on input values. */
int
rigel_rc_load(const char *rigelrc, struct device **dev, size_t max_devices)
{
    FILE *fp;
    char line[256];
    int d = 0, l = 0;

    memset(dev, 0, max_devices * sizeof(struct dev *));
    
    if(!(fp = rigel_get_rc(rigelrc))) {
        rigel_error("Unable to find suitable configuration file!\n");
        return -1;
    }
    
    while(!feof(fp)) {
        if(d >= max_devices) {
            rigel_warn("Device count in configuration file %s exceeds maximum "
                       "number of devices for this program (%zu)! Ignoring rest.",
                       rigelrc, max_devices);
                   
            dev[max_devices] = NULL;
            fclose(fp);
            
            return 0;
        }
        
        memset(line, 0, sizeof(char));
        fgets(line, 256, fp);
        l++;
        
        if(!strlen(line))
            continue;
        
        switch(line[0]) {
        case ' ':
        case '\r':
        case '\n':
        case '#':
            continue;
        
        case 'd':        
            dev[d] = (struct device *)malloc(sizeof(struct device));
            memset(dev[d], 0, sizeof(struct device));
            
            if(sscanf(line, "d:%04hX:%s",
                      &dev[d]->dev_id,
                      dev[d]->dev_name) != 2) goto parse_err;
            break;
        
        case 'p':
            if(sscanf(line, "p:%06X:%06X",
                      &dev[d]->mem.flash_low,
                      &dev[d]->mem.flash_high) != 2) goto parse_err;
            break;
            
        case 'e':
            if(sscanf(line, "e:%06X:%06X",
                      &dev[d]->mem.eeprom_low,
                      &dev[d]->mem.eeprom_high) != 2) goto parse_err;
            break;       
             
        case 'c':
            if(sscanf(line, "c:%06X:%06X",
                      &dev[d]->mem.config_low,
                      &dev[d]->mem.config_high) != 2) goto parse_err;
            break;
        
        case 'm':
            if(sscanf(line, "m:%hhd:%d:%d",
                      &dev[d]->opts.max_packet_size,
                      &dev[d]->opts.wlag,
                      &dev[d]->opts.rlag) != 3) goto parse_err;
            d++;
            break;
        
        default:
            rigel_error("rigelrc: malformed directive: %s\n", line);
            goto parse_err;
        }    
    }
    
    fclose(fp);    
    dev[d] = NULL;
    
    return d;
    
parse_err:
    rigel_error("rigelrc: parse error, line %d.\n", l);
    fclose(fp);

    /* Free any memory we could've allocated until the parse error. */
    for(l = 0; l <= d && dev[l]; l++)
        free(dev[l]);
    
    dev = NULL;
    
    return -1;
}

void
rigel_rc_free(struct device **devlist)
{
    int i;
    for(i = 0; devlist[i]; i++)
        free(devlist[i]);
}

/* Example callback for the device API. Used in the CLI interface.
 * Creates a 'progress bar' of sorts by outputting # marks over
 * the course of the device operation. 
 * Modified version of printHashes() from rpminstall */
void
load_update(uint32_t cur, uint32_t total)
{
    static uint32_t old = 0;
    int needed, i;
    
    if(old > cur)
        old = hash_current = 0;
        
    if(hash_current != hash_total) {
        float pct = (float)cur / total;
        needed = hash_total * pct + 0.5;
        
        while(needed > hash_current) {
            for(i = 0; i < hash_current; i++)
                putchar('#');
            for(; i < hash_total; i++)
                putchar(' ');
            printf("[%3d%%]", (int)((100 * pct) + 0.5));
            for(i = 0; i < hash_total + 6; i++)
                putchar('\b');
            
            hash_current++;
	        old = cur;
        }
        fflush(stdout);
        
        if (hash_current == hash_total) {
	        for (i = 1; i < hash_current; i++)
		        putchar ('#');
		    printf(" [100%%]\n");
	}
	fflush(stdout);
    }
}

int
rigel_memdump(struct device *dev, const char *file, 
              int reg, int format, uint32_t *size)
{
    int ret;
    FILE *b;
    uint8_t *mem = NULL;
    uint32_t start, end;
    size_t region_size = 0;

    if(!size)
         return -1;
    
    switch(reg) {
    case USER_PROC_FLASH:
        region_size = dev->mem.flash_high + 1;
        start = dev->mem.flash_low;
        end   = dev->mem.flash_high;
        
        break;
    
    case USER_BOOT_SECTOR:
        region_size = end = dev->mem.flash_low;
        start = 0;
        
        break;
    
    case USER_EEPROM_DATA:
        start = 0;
        end   = dev->mem.eeprom_high;
        region_size = dev->mem.eeprom_high;
        
        break;
    
    default:
        return -1;
    }
    
    /* The reason for allocating end bytes of memory for the dump
     * is because we are directly referencing offsets of memory in the
     * controller. Since the region we are dumping may not start at 0,
     * if we only allocate region_size bytes, we will overrun the buffer.
     * It is not terribly efficient to waste 128KB of memory in the case of
     * reading user memory, but it greatly simplifies the following code,
     * and the memory is freed almost immediately after. */
    mem = (uint8_t*)malloc(end);
    if(!mem) {
        rigel_error("allocating memory for dump operation!\n");
        return -1;
    }
    /* Emulate the controller's erase operation (set all bits). */
    if(dev->is_ifi)
         memset(mem, 0x00, end);
    else
         memset(mem, 0xFF, end);
        
    if(reg == USER_PROC_FLASH)
        ret = end = rigel_read_user(dev, mem, end);
    else if(reg == USER_BOOT_SECTOR)
        ret = rigel_read_loader(dev, mem, end);
    else ret = device_read_eeprom(dev, 0, region_size, mem);
    
    if(ret == -1) {
        free(mem);
        return -1;
    }
    
    switch(format) {
    case IntelHexFormat:
         inhex32_write(file, mem, start, end);
         break;

    case InnovationFirstFormat:
         ifi_bin_write(file, mem, start, end);
         break;
         
    case BinaryDataFormat:
        b = fopen(file, "wb");
        if(!b) {
            rigel_error("opening file for binary dump\n");
            return -1;
        }
        fwrite(mem, 1, end, b);
        fclose(b);
        
        break;
    }
        
    free(mem);
    *size = end;
    
    return 0;
}
