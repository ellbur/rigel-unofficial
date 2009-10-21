/* -*- mode: C; c-file-style: "k&r"; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* librigel API for communicating with a PIC16/PIC18F device using the AN851
 *  protocol over a TTY device (i.e., RS-232). */

/*  Copyright (C) Harry Bock 2006, 2007 <hbock@providence.edu>
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

#include "an851.h"
#include "pic18.h"
#include "serialio.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static struct an851_config opts;

static int an851_checksum(struct an851_packet *p);
static int an851_tx(struct an851_packet *tx,
                    struct an851_packet *rx);

int
an851_safe_init(int fd)
{
    if(sio_valid(fd) == -1) {
        rigel_error("AN851 init: cannot set invalid fd!\n");
        return -1;
    }
    
    opts.fd = fd;
    
    opts.rlag = 2;
    opts.wlag = 5;
    opts.reset_lag = 1000000;
    
    return 0;
}

int
an851_init(int fd, int wlag, int rlag, struct pic18_memory_layout mmap)
{
    if(sio_valid(fd) == -1) {
        rigel_error("AN851 init: cannot set invalid fd!\n");
        return -1;
    }
    opts.fd   = fd;
    opts.mem  = mmap;
    
    opts.wlag = wlag;
    opts.rlag = rlag;
    opts.reset_lag = 1000000;
    
    opts.lastcmd  = 0;
    opts.lastaddr = 0x000000;
    
    return 0;
}

int
an851_rd(byte command, dword address, byte length, void *rx)
{
    byte *d;
    int rxlen;
    struct an851_packet rd_command, rd_data;

    /* We should only get here if we are reading flash, EEPROM data,
     * or configuration data */
    assert(rx && (command == RD_FLASH  ||
                  command == RD_EEDATA ||
                  command == RD_CONFIG));

    rd_command.command = command;
    rd_command.length = 4;
    
    /* All AN851 read commands occur on the byte level, so we can
     * specify our request_length without worrying about blocks
     * or rows. */
    rd_command.request_length = length;
    
    rd_command.data[0] = length;
    rd_command.data[1] = ADDRL(address);
    rd_command.data[2] = ADDRH(address);
    rd_command.data[3] = ADDRU(address);

    /* Bubble the error up to give a more descriptive message
     * whereever this function was actually called */
    if(an851_tx(&rd_command, &rd_data) == -1)
        return -1;

    d = rd_data.data;
    rxlen = d[0];
    
    /* We must assure that the device's response to a read request
     * matches the original request. */
    if(rxlen != length || ADDRESS(d[1], d[2], d[3]) != address) {
        rigel_error("Malformed read response from device!\n");
        return -1;
    }

    /* Copy just the data back to the supplied buffer.
     * The PIC sends back the command/length/address
     * but we only want to retain the data itself. */
    memcpy(rx, rd_data.data + 4, rxlen);
    
    return rxlen;
}

int
ifi_run_program(void)
{
    struct an851_packet run;
    
    run.command = IFI_RUN_CODE;
    run.data[0] = 0x40;
    run.length  = 1;
    
    return an851_tx(&run, &run);
}

int
ifi_wr_row(dword addr, byte rows, byte val)
{
    struct an851_packet wr_row, wr_response;
    
    wr_row.command = IFI_WR_ROW;
    
    wr_row.length  = 5;
    wr_row.request_length = rows;
    
    wr_row.data[0] = rows;
    wr_row.data[1] = ADDRL(addr);
    wr_row.data[2] = ADDRH(addr);
    wr_row.data[3] = ADDRU(addr);
    wr_row.data[4] = val;
    
    return an851_tx(&wr_row, &wr_response);
}
int
an851_reset(void)
{
    struct an851_packet reset;

    reset.command = PIC_RESET;
    reset.length = 1;
    reset.data[0] = 0x00;

    an851_tx(&reset, &reset);
    waitus(opts.reset_lag);

    return 0;
}

/* Read and return the version of the bootloader used. 
 * The version is a 16-bit value - 0xAABB, with AA being
 * the major versinon BB being the minor. The FRC returns
 * 0x0101, or v1.1 */
int
an851_version(void)
{
    struct an851_packet version_tx, version_rx;

    version_tx.command = RD_VERSION;
    version_tx.length = 1;
    version_tx.request_length = 2;
    
    version_tx.data[0] = 0x02;

    if(an851_tx(&version_tx, &version_rx) == -1)
        return -1;

    return MAKEWORD(version_rx.data[1], version_rx.data[2]);
}

int
an851_rd_flash(dword address, byte length, void *flashdata)
{
    return an851_rd(RD_FLASH, address, length, flashdata);
}

int
an851_rd_eeprom(word address, byte length, void *eedata)
{
    return an851_rd(RD_EEDATA, address, length, eedata);
}

int
an851_rd_config(dword confaddr, byte length, void *configdata)
{
    return an851_rd(RD_CONFIG, confaddr, length, configdata);
}

/* Write any number of blocks (8 bytes each) to program (flash) memory.
 * WARNING: This function's behavior is ONLY defined if data is aligned to
 * an 8-byte boundary (i.e. is a multiple of 8). If data is not actually
 * of size blocks * BYTES_PER_BLOCK, you risk reading past the buffer.
 * It is the responsibility of the caller to ensure this alignment. */
int
an851_wr_flash(dword address, byte blocks, void *data)
{
    struct an851_packet wr_flash, ack;
    dword bytelen = blocks * BYTES_PER_BLOCK;
    
    wr_flash.command = WR_FLASH;
    wr_flash.length = bytelen + 4;
    wr_flash.request_length = bytelen;

    wr_flash.data[0] = blocks;
    wr_flash.data[1] = ADDRL(address);
    wr_flash.data[2] = ADDRH(address);
    wr_flash.data[3] = ADDRU(address);

    memcpy(wr_flash.data + 4, data, bytelen);
    
    return an851_tx(&wr_flash, &ack);
}

/* Erase rows (64 bytes each) of flash memory starting at address. */
int
an851_er_flash(dword address, byte rows)
{
    struct an851_packet er_flash, ack;
    
    er_flash.command = ER_FLASH;
    er_flash.length = 4;
    er_flash.request_length = rows;
    
    er_flash.data[0] = rows;
    er_flash.data[1] = ADDRL(address);
    er_flash.data[2] = ADDRH(address);
    er_flash.data[3] = ADDRU(address);

    return an851_tx(&er_flash, &ack);
}

int
an851_wr_eeprom(word address, byte length, void *data)
{
    struct an851_packet wr_eedata, ack;

    wr_eedata.command = WR_EEDATA;
    wr_eedata.length = length + 4;
    wr_eedata.request_length = length;
    
    wr_eedata.data[0] = length;
    wr_eedata.data[1] = ADDRL(address);
    wr_eedata.data[2] = ADDRH(address);
    wr_eedata.data[3] = 0x00;

    memcpy(wr_eedata.data + 4, data, length);
    
    return an851_tx(&wr_eedata, &ack);
}

int
an851_wr_config(byte confaddr, byte length, void *data)
{
    struct an851_packet wr_config, ack;

    wr_config.command = WR_CONFIG;
    wr_config.length = length + 3;
    wr_config.request_length = length;
    
    wr_config.data[0] = length;
    wr_config.data[1] = confaddr;
    wr_config.data[2] = 0x00;
    wr_config.data[3] = 0x30;

    memcpy(wr_config.data + 4, data, length);

    return an851_tx(&wr_config, &ack);
}

int
an851_replicate_write(byte write_command, byte length, dword address)
{
    struct an851_packet wr_command, wr_ack;

    wr_command.command = write_command;
    wr_command.length = 5;
    wr_command.request_length = length;
    
    wr_command.data[0] = length;
    wr_command.data[1] = ADDRL(address);
    wr_command.data[2] = ADDRH(address);
    wr_command.data[3] = ADDRU(address);

    return an851_tx(&wr_command, &wr_ack);
}

int
an851_repeat(void)
{
    struct an851_packet rp_com, rp_ack;
    
    rp_com.command = opts.lastcmd;
    rp_com.length = 0;
    
    return an851_tx(&rp_com, &rp_ack);
}

int
an851_wait_response(byte *buf, size_t max)
{
    int prev, wait = 0;
    int recv = sio_read(opts.fd, buf, max);
    
    if(!recv) return 0;
    if(recv == -1) return -1;
    
    while(!(buf[recv-1] == ETX && buf[recv-2] != DLE)) {
        prev  = recv;
        recv += sio_read(opts.fd, &buf[recv], max-recv);
        
        if(prev - recv == 0)
             wait++;
        else wait = 0;
        
        /* If we don't read anything twice in a row, it's
         * pretty clear we didn't get a response. */
        if(wait >= 2)
            return -1;
    }
    return recv;
}
    
static int
an851_checksum(struct an851_packet * p)
{
    int c, chk;

    chk = p->command;
    for(c = 0; c < p->length; c++)
        chk += p->data[c];
        
    return ((~chk + 1) & 0xFF);
}

static int
an851_tx(struct an851_packet *tx, struct an851_packet *rx)
{
    int i;
    int transmit_len, datalen, recv_len, retry = 0;
    static byte buffer[MAX_PACKET_SIZE * 2];
    
    if(!tx || !rx) {
        rigel_error("invalid argument!\n");
        return -1;
    }    
    if(sio_valid(opts.fd) == -1) {
        rigel_error("fd invalid!\n");
        return -1;
    }
    if(tx->length+1 > MAX_PACKET_SIZE) {
        rigel_error("Error preparing data for transmission: "
                    "Packet size %u is too big for bootloader.\n", tx->length);
        return -1;
    }
    
__retry:    
    datalen = transmit_len = recv_len = 0;    
    opts.lastcmd = tx->command;
    tx->checksum = an851_checksum(tx);
    
    /* Start our packet off with 2 * STX (start of text)
     * <STX><STX><Command><DataLength><Data><Checksum><ETX> */
    buffer[0] = STX;
    buffer[1] = STX;
    
    if(IS_CONTROL(tx->command)) {
        buffer[2] = DLE;
        buffer[3] = tx->command;
        transmit_len = 4;
    } else {
        buffer[2] = tx->command;
        transmit_len = 3;
    }

    /* Escape all control characters within the data as we copy */
    for(i = 0; i < tx->length; i++, transmit_len++) {
        if(IS_CONTROL(tx->data[i])) {
            buffer[transmit_len] = DLE;
            transmit_len++;
        }
        buffer[transmit_len] = tx->data[i];
    }
    
    /* Insert checksum */
    if( IS_CONTROL(tx->checksum) ) {
        buffer[transmit_len++] = DLE;
        buffer[transmit_len++] = tx->checksum;
    } else buffer[transmit_len++] = tx->checksum;
    
    /* Mark the end of the buffer with ETX (end of text) */
    buffer[transmit_len++] = ETX;

    if(sio_write(opts.fd, buffer, transmit_len) == -1) {
        rigel_error("I/O error transmitting data to PIC!");
        return -1;
    }
    
    /* Set timeouts for our device to wait for a response.
     * If there is no response within this time, this generally
     * means an error occured. */
    switch(tx->command) {
    
    /* These commands do not have responses */
    case PIC_RESET:
    case IFI_RUN_CODE:
        return 0;
    
    case RD_FLASH:
    case RD_CONFIG:
    case RD_EEDATA:
    case RD_VERSION:
        sio_settimeout(opts.rlag * tx->request_length);
        break;
    
    case WR_FLASH:
    case WR_CONFIG:
    case WR_EEDATA:
    case IFI_WR_ROW:
        sio_settimeout(opts.wlag * tx->request_length);
        break;
    
    case ER_FLASH:
        sio_settimeout(opts.wlag * 0xFF);
        break;
        
    default:
        break;
        
    }
        
    memset(rx, 0, sizeof(struct an851_packet));
    
    /* When we don't get a proper reponse, we retry up to 3 times.
     * If we don't succeed on the fourth try, bail out. */
    recv_len = an851_wait_response(buffer, sizeof(buffer));
    if( (!recv_len || recv_len == -1) && retry < 3) {
        retry++;
        goto __retry;
    }
        
    if(!recv_len)
        return -1;
        
    /* Strip control characters from received data */
    for(i = 0, datalen = 0; i < recv_len; i++) {
        if(IS_CONTROL(buffer[i])) {
            /* Skip over the escape character and copy next data byte */
            if(buffer[i] == DLE) {
                i++; 
                rx->data[datalen++] = buffer[i];
            } else i++; /* Just skip over other control characters */
        } else {
            rx->data[datalen] = buffer[i];
            datalen++;
        }
    }
        
    rx->command = rx->data[0];
    if(tx->command != rx->command)
        return -1;

    memmove(rx->data, rx->data + 1, datalen);

    /* Write commands ack with only the command number, they do not
     * send any other data. */
    switch (rx->command) {
    case WR_FLASH:
    case ER_FLASH:
    case WR_EEDATA:
    case WR_CONFIG:
        rx->length = 0;
        break;

    default: rx->length = datalen; break;
    }

    /* Checksum is the last data byte in any packet */
    rx->checksum = rx->data[rx->length];
    if( (byte)an851_checksum(rx) != rx->checksum ) {
        rigel_error("checksum mismatch! Expected %02X, calculated %02X\n",
                    rx->checksum, an851_checksum(rx));
        return -1;
    }

    return retry;
}
