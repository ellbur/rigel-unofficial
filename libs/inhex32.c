/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* librigel API for reading and writing Intel HEX32 files into memory. Supports
 * both extended linear and segment addressing. */

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
#include "inhex32.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int
inhex32_parse_line( const char *line,
                    struct inhex32_record *rec )
{
    int c, chk;
    static uint16_t address_ext = 0;
    static int address_mode = LiteralAddressMode;
    
    if(!line || *line++ != ':')
         return -1;

    /* Scan out the following ASCII hex data: byte dword byte */
    if(sscanf(line, "%02hhX%04X%02hhX", &rec->length,
              &rec->address, &rec->record_type) != 3)
         return -1;

    /* We have reached the end of the hex file, stop. */
    if(rec->record_type == INHEX_EOF)
        return 0;

    /* Skip to the data section of the record and read rec.length bytes */
    line += 8;
    for(c = 0; c < rec->length; c++) {
        if(sscanf(line, "%02hhX", &rec->data[c]) != 1)
            return -1;
        
        line += 2;
    }

    /* Get the checksum and validate our data against it */
    if(sscanf(line, "%02hhX", &rec->checksum) != 1)
        return -1;

    /* Intel HEX checksum == two's complement of the sum of the bytes
     * represented by the file (includes address and length) */
    chk =  rec->length;
    chk += rec->record_type;
    chk += HIBYTE(rec->address) + LOBYTE(rec->address);

    for(c = 0; c < rec->length; c++)
        chk += rec->data[c];

    chk = ((~chk & 0xFF) + 1) & 0xFF;
    if(chk != rec->checksum) {
        rigel_error("Parse error: checksum mismatch - calculated %02X, "
                    "required %02X\n", chk, rec->checksum);
        return -1;
    }
    
    /* TODO: Make this endian-neutral. */
    switch(rec->record_type) {
    case INHEX_DATA:
        if(address_mode == SegmentAddressMode)
            SET_EXT_SEGMENT(rec->address, address_ext);
        
        else if(address_mode == LinearAddressMode)
            SET_EXT_LINEAR(rec->address, address_ext);

        break;
        
    case INHEX_EXT_SEGMENT:
        address_mode = SegmentAddressMode;
        address_ext = MAKEWORD(rec->data[1], rec->data[0]);
        break;
    
    case INHEX_EXT_LINEAR:
        address_mode = LinearAddressMode;
        address_ext = MAKEWORD(rec->data[1], rec->data[0]);
        break;
    
    default:
        rigel_error("Parse error: unsupported record type %02X\n", rec->record_type);
        return -1;
    }                  
    
    return 0;
}

int
inhex32_validate(const char *fn)
{
    int line = 0;
    char buf[64];
    struct inhex32_record rec;
    
    FILE *fp = fopen(fn, "r");
    if(!fp) {
        rigel_error("could not open file %s!\n", fn);
        return -1;
    }
    
    while(!feof(fp)) {
        fgets(buf, 64, fp);
        line++;

        if(inhex32_parse_line(buf, &rec) == -1) return -1;
        if(rec.record_type == INHEX_EOF) break;
    }
    
    fclose(fp);
    
    return 0;
}
    
int
inhex32_write(const char *fn, void *pmem, uint32_t start, uint32_t end)
{
    int c, chk;
    uint16_t  ext = 0;
    uint32_t addr;
    uint8_t len = INHEX_MAX_DATA;
    uint8_t *mem = (uint8_t*)pmem;
    
    FILE *out = fopen(fn, "w");

    if(!out) {
        rigel_error("creating HEX output file %s!\n", fn);
        return -1;
    }
    
    fprintf(out, ":020000040000FA\r\n");
    
    for(addr = start; addr < end; addr += len) {
        if((end - addr) < len)
            len = (end - addr);
        
        /* If the current address exceeds the capacity of a word,
         * use the extended linear address directive. */
        if(ext != HIWORD(addr)) {
            ext = HIWORD(addr);
            chk = 0x06 + HIBYTE(ext) + LOBYTE(ext);
            chk = ((~chk & 0xFF) + 1) & 0xFF;
            
            fprintf(out, ":02000004%04X%02X\r\n", ext, chk);
        }
        
        /* It is necessary to mask out the high byte of addr because
         * we can only output the last 2 bytes of the address. */
        fprintf(out, ":%02X%04X%02X", len, addr & 0xFFFF, INHEX_DATA);
        for(c = 0; c < len; c++)
            fprintf(out, "%02X", mem[addr + c]);

        chk =  len;
        chk += HIBYTE(addr);
        chk += LOBYTE(addr);
        chk += INHEX_DATA;

        for(c = 0; c < len; c++)
            chk += mem[addr+c];
        chk += 5 - 5;
        chk = ((~chk & 0xFF) + 1) & 0xFF;
        
        /* End data segment with the checksum and newline (DOS) */
        fprintf(out, "%02X\r\n", (uint8_t)chk);
    }
    
    /* Print out the EOF and commit changes */
    fprintf(out, ":00000001FF\r\n");
    fclose(out);
    
    return 0;
}

/* Parse an Intel HEX file (output from mplink) and return
 * a map of the PIC memory we are to write. */
int
inhex32_read(const char *fn, void *pmem, 
             size_t memsize, uint32_t *start, uint32_t *end)
{
    int line = 0;
    char buf[64];
    struct inhex32_record rec;
    uint8_t *picmem = (uint8_t*)pmem;
    int warn_config_data = 0;

    FILE *fp = fopen(fn, "r");

    if(start && end)
        *start = *end = 0;
    
    if(!fp) {
        rigel_error("could not open file %s!\n", fn);
        goto error;
    }
    
    memset(&rec, 0x00, sizeof(struct inhex32_record));
    if(pmem)
         memset(pmem, 0xFF, memsize);
    
    while(!feof(fp)) {
        fgets(buf, 64, fp);
        line++;

        if(inhex32_parse_line(buf, &rec) == -1) {
            rigel_error("Parsing inhex32 file at line %d.\n", line);
            return -1;
        }
        
        if(rec.record_type == INHEX_EOF) break;
        
        /* If this is a data record, copy to picmem at the correct addr
         * and adjust max/min program addresses */
        if(rec.record_type == INHEX_DATA) {
            if(rec.address + rec.length > memsize) {
                /* For some reason, MCC18 + MPLINK may generate configuration
                 * data in the INHEX32 file. This data is not writable on the
                 * FRC, so we ignore it (once per file). */
                if(rec.address & CONFIG_REGISTER_MASK) {
                    if(pmem && !warn_config_data) {
                         rigel_warn("Ignoring configuration register data present in %s.\n", fn);
                         warn_config_data = 1;
                    }
                    continue;
                } else {
                    rigel_error("buffer too small for input program!\n");
                    goto error;
                }
            }
            if(pmem)
                 memcpy(&picmem[rec.address], rec.data, rec.length);

            if(start && end) {
                if(*start == 0 && rec.address != 0)
                    *start = rec.address;

                *start = min(*start, rec.address);
                *end   = max(*end,   rec.address + rec.length);
            }
        }
    }
    
    fclose(fp);
    return 0;

  error:
    if(fp)
        fclose(fp);
    return -1;
}

int
ifi_bin_read(const char *fn, void *buffer, size_t bufsize, 
             uint32_t *start, uint32_t *end)
{    
    int i, line_no = 1;
    FILE *bin;
    uint8_t *data;
    uint32_t address, read_start, read_end;
    char line[IFIBIN_LINE_LEN+1], *temp;

    address = read_start = read_end = 0;
    data = (uint8_t*)buffer;
    
    if( !(bin = fopen(fn, "r")) ) {
        rigel_error("opening %s for reading!\n", fn);
        return -1;
    }

    fscanf(bin, "%06X", &read_start);
    fseek (bin, -IFIBIN_LINE_LEN, SEEK_END);
    fscanf(bin, "%06X", &read_end);
    
    read_end += IFIBIN_DATA_LEN;
    
    if(read_end > bufsize) {
        rigel_error("IFI .bin parser: buffer too small for program data!\n");
        fclose(bin);
        return -1;
    }
    
    if(data)
         memset(data, 0xFF, bufsize);

    while(!feof(bin)) {
        fgets(line, IFIBIN_LINE_LEN+1, bin);
        temp = line;
        
        if(sscanf(temp, "%06X ", &address) != 1)
            goto error;

        temp += 7;

        if(!data)
             continue;
        
        for(i = 0; i < IFIBIN_DATA_LEN; i++) {
            if(sscanf(temp, "%02hhX", &data[address+i]) != 1)
                goto error;
                
            temp += 2;
        }
    }
    fclose(bin);

    if(start && end) {
         *start = read_start;
         *end = read_end;
    }
    return 0;

error:
    rigel_error("IFI .bin parse error on line %d!\n", line_no);
    fclose(bin);
    
    return -1;
}
        
int
ifi_bin_write(const char *fn, void *pmem, uint32_t start, uint32_t end)
{
    FILE *out;
    uint8_t len = IFIBIN_DATA_LEN;
    uint8_t i, *data = (uint8_t*)pmem;
    uint32_t addr;
    
    if( !(out = fopen(fn, "w")) ) {
        rigel_error("Cannot open %s for writing!\n", fn);
        return -1;
    }
    
    for( addr = start; addr < end; addr += len ) {
        if(end - addr < len)
            len = (end - addr);

        fprintf(out, "%06X", addr);
        
        for(i = 0; i < len; i++)
            fprintf(out, " %02X", data[addr+i]);
        
        /* OH GOD THE INDENTATION!!! */
        if(len < IFIBIN_DATA_LEN) {
            for(i = 0; i < IFIBIN_DATA_LEN - len; i++) 
                fprintf(out, " %02X", 0xFF);
        }
        
        fprintf(out, "\r\n");
    }
    fclose(out);
    
    return 0;
}
