/*  Command-line implementation of a program loader for PIC18F devices
 *  using the AN851 in-circuit loading protocol. */

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

#include "rigel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BLUE(x) "\e[1;36m" x "\e[0m"
#define BOLD(x) "\e[1m" x "\e[0m"

extern char *optarg;
extern int optopt, optind, errno;
extern int errno;

static rigel_t options;
static struct option longopts[] = {
    { "serial",   optional_argument, NULL, 's' },
    { "read",     optional_argument, NULL, 'd' },
    { "run",      optional_argument, NULL, 'r' },
    { "term",     optional_argument, NULL, 't' },
    { "dump-all", optional_argument, NULL, 'a' },
    { "devlist",  required_argument, NULL, 'l' },
    { "format",   required_argument, NULL, 'f' },
    { "no-ifi",   no_argument,       NULL, 'i' },
    { "rambots",  no_argument,       NULL, 'z' },
    { "master",   no_argument,       NULL, 'm' },
    { "eeprom",   no_argument,       NULL, 'p' },
    { "erase",    no_argument,       NULL, 'e' },
    { "configreg",no_argument,       NULL, 'c' },
    { "verify",   no_argument,       NULL, 'v' },
    { "help",     no_argument,       NULL, 'h' },
    { NULL,       0,                 NULL, 0   }
};

void
sigint( int sig )
{
    options.interrupt = 1;
}

void
capture_output( device_t *dev, const char *out )
{
    FILE *outf;
    ssize_t len;
    char temp[128];

    printf("Beginning serial output capture from device %s. For better \n"
           "communication with your device, please use a true terminal \n"
           "emulator (i.e., minicom on UNIX or HyperTerminal on Windows) \n"
           "or the GUI front-end to this program.\n");    
           
    if(!out)
         outf = stdout;
    else outf = fopen(out, "wb");
    
    if( !outf || sio_valid(dev->opts.fd) == -1 ) {
        device_disconnect(dev);
        rigel_fatal("opening/creating output capture file(s).\n");
    }    
    while(!options.interrupt) {
        len = sio_read(dev->opts.fd, temp, 128);
        if(len == -1) {
            rigel_error("capturing output from device\n");
            device_disconnect(dev);
            return;
        }
        fwrite(temp, 1, len, outf);
        fflush(outf);
    }
    printf("Completed serial capture!\n\n");
}

void
print_config(const device_t *dev)
{   
    int i;
    pic18_config_t config = dev->config;
    
    printf( BOLD("%s Device Configuration\n-----------------\n"),
            dev->dev_name);
    
    printf("Device Memory Protection:\n");
    for(i = 0; i < 8 && ((i+1) * FLASH_BLOCK_SIZE - 1) <= dev->mem.flash_high; 
               i++) {
        printf("Flash block %d [%06X-%06X]: ", i, 
               (i == 0) ? dev->mem.flash_low : i*FLASH_BLOCK_SIZE,
               ((i+1) * FLASH_BLOCK_SIZE) - 1);
        
        if(!(config.C5L & CP(i)))   printf("[CODE] ");
        if(!(config.C6L & WRT(i)))  printf("[WRITE] ");
        if(!(config.C7L & EBRT(i))) printf("[TABLE READ]");
        
        printf("\n");
    }
    
    printf("EEPROM [%06X-%06X]: ", dev->mem.eeprom_low, dev->mem.eeprom_high);
    if(!(config.C5H & CPD))  printf("[CODE] ");
    if(!(config.C6H & WRTD)) printf("[WRITE]");
    
    printf("\nBoot Block [000000-%06X]: ", dev->mem.flash_low-1);
    if(!(config.C5H & CPB))  printf("[CODE] ");
    if(!(config.C6H & WRTB)) printf("[WRITE]");
    
    printf("\nConfiguration Registers: ");
    if(!(config.C6H & WRTC)) printf("PROTECTED");
    
    printf( BOLD("\n\nOther options:\n-----------------\n") );
    printf("Instruction set mode: %s\n", (config.C4L & XINST) ?
                                         "Extended" : "Legacy");
                                         
    printf("Stack Full/Underflow Reset: %s\n",
          (config.C4L & STVREN) ? "ENABLED" : "DISABLED");
    printf("In-Circuit Debug %s\n", 
          (config.C4L & DEBUG) ?  "ENABLED" : "DISABLED");
                                                
    fputc('\n', stdout);
}

int
main(int argc, char **argv)
{
    uint8_t *prog, was_ifi;
    uint32_t start, end, read_size;
    int c, ndev;

    struct device rdev, *devices[CONFIG_MAX_DEVICES];

    prog = NULL;
    was_ifi = 0;
    memset(&options, 0, sizeof(struct rigel_options));
    options.run = 1;
    options.fmt = IntelHexFormat;
    
    while((c = getopt_long(argc, argv, "mcpvihzf:l:a::r::t::s:d::",
                           longopts, NULL)) != -1) {
        switch (c) {
        case 's':
	    
            if(!optarg) {
		printf("no optarg specified -- using default\n");

                options.device = DEFAULT_SERIAL_PORT;
                rigel_warn("No serial port specified, trying "
                     "default device %s.\n\n", DEFAULT_SERIAL_PORT);
            }
            else options.device = optarg;
            break;
        
        case 't': 
            options.term = 1; 
            if(optarg)
                options.fterm = optarg;
            break;
        
        case 'l': options.etc    = optarg; break;
        case 'c': options.conf   = 1; break;
        case 'v': options.verify = 1; break;
        case 'p': options.eeprom = 1; break;
        case 'e': options.erase  = 1; break;
        case 'm': options.master = 1; break;
        case 'i': options.noifi  = 1; break;
        
        case 'd':
            if(optarg && strncasecmp(optarg, "boot", 4) == 0)
                 options.dump = USER_BOOT_SECTOR;
            else if(optarg && strncasecmp(optarg, "eeprom", 6) == 0)
                 options.dump = USER_EEPROM_DATA;
            else options.dump = USER_PROC_FLASH;
            break;
            
        case 'f':
            if(strncasecmp(optarg, "hex", 3) == 0)
                options.fmt = IntelHexFormat;
            else if(strncasecmp(optarg, "bin", 3) == 0)
                options.fmt = InnovationFirstFormat;
            else if(strncasecmp(optarg, "raw", 3) == 0)
                options.fmt = BinaryDataFormat;
                
            else rigel_fatal("invalid file format %s specified; "
                       "must be one of hex, bin, raw.\n", optarg);

            break;

        case 'r':
            if(optarg && strncasecmp(optarg, "no", 2) == 0) 
                options.run = 0;
                
            break;
        
        case 'z':
            printf("<Patrician|Away> what does your robot do, sam\n"
                   "<bovril> it collects data about the surrounding "
                   "environment,\n         then discards it and drives "
                   "into walls\n");
            return 0;
            
        case 'h':
            options.help = 1;
            
        case '?':
        default:
            goto usage;
        }
    }
  
    ndev = rigel_rc_load(options.etc, devices, CONFIG_MAX_DEVICES);

    if (ndev == -1 && !options.etc) {
    	printf("You should specify a rigelrc file with -l. There should be "
	       "one in the source tree called 'rigelrc'.\n");
	printf("Anyway so since we don't have one you'll probably see "
	       "horid errors in the next step.\n");
    }

    if(ndev == -1)
        rigel_fatal("Could not load device configuration list %s\n", options.etc);
        
    /* If in dump or load mode, make sure an output/input file was 
     * specified. */
    if(optind < argc)
        options.file = argv[optind];
        
    else if(options.dump || (!options.term && !options.conf && !options.erase)) {
        rigel_rc_free(devices);
        goto usage;
    }
    
    if(!options.device)
        options.device = DEFAULT_SERIAL_PORT;
    
    if(device_connect(options.device, &rdev, devices, ndev) == -1) {
        rigel_rc_free(devices);
        rigel_fatal("Connecting to device failed, aborting!\n");
    }

    /* Thanks valgrind :D */
    rigel_rc_free(devices);
    
    printf("Connection to device on %s successful, AN851 v%d.%d (%s).\n"
           "Controller " BOLD("%s") " identified. "
           "Boot block size: %d bytes\n\n", options.device, 
           AN851_MAJOR_VER(rdev.bootver),
           AN851_MINOR_VER(rdev.bootver),
           (rdev.is_ifi) ? "IFI" : "non-IFI",
           rdev.dev_name, rdev.mem.flash_low);
           
    if(options.noifi && rdev.is_ifi) {
        printf("Ignoring IFI erase extensions for this transaction.\n");
        rdev.is_ifi = 0;
        was_ifi = 1;
    }
    
    if(options.conf)
        print_config(&rdev);
    
    if(options.erase && !options.file && !options.dump) {
        printf( BLUE("Erasing: ") );
        
        device_set_callback(&rdev, load_update);
        rigel_erase_device(&rdev);
        
        printf("Complete!\n\n");
        goto cleanup;
    }
    
    if(options.dump) {
        if(options.erase)
            rigel_warn("Ignoring erase flag specified with read operation.\n");
        
        /* The user might try to do --read --eeprom instead of
         * --read=eeprom */
        if(options.eeprom)
            options.dump = USER_EEPROM_DATA;
        
        printf( BLUE("Reading device: ") );
        
        device_set_callback(&rdev, load_update);
        if(rigel_memdump(&rdev, options.file, options.dump,
                         options.fmt, &read_size) == -1) {
            rigel_error("Device read failed!\n");
            goto r_error;
        }
        printf("Complete! Read 0x%06X bytes to %s.\n", read_size, options.file);
        
        goto cleanup;
    }
    
    if(options.file) {
        /* Allocate enough memory for the largest possible program
         * (128K on a PIC18F8722, for example), and attempt to read
         * the HEX file into this program. */
        rdev.opts.verify_on_write = options.verify;

        device_set_callback(&rdev, load_update);
                
        /* Force write verification and IFIBIN format for master updates. */
        if(options.master) {
            printf("FRC master processor firmware update:\n"
                   "\tThis will overwrite your controller's current program;\n"
                   "\tplease remember to reload after the update.\n");
            rdev.opts.verify_on_write = 1;
            options.fmt = InnovationFirstFormat;
        }
        
        /* Map our program to memory so we know if it is valid before we
         * wipe the user's device. :) */
        if(!(prog = rigel_program_alloc(&rdev, options.file, options.fmt,
					&start, &end))) {
            rigel_error("mapping program file to memory!\n");
            goto r_error;
        }
        
        /* Loading binary image to data EEPROM */
        if(options.eeprom) {
            printf("Loading %d bytes of binary data to device EEPROM.\n"
                   BLUE("Write: "), end - start);
            
            if(end > rdev.mem.eeprom_high) {
                rigel_error("Binary data will not fit in EEPROM! limit your data "
			    "to %d bytes.\n", rdev.mem.eeprom_high);
                goto r_error;
            }
            if(device_write_eeprom(&rdev, start, end - start, prog) == -1) {
                rigel_error("EEPROM write failed! Aborting.\n");
                goto r_error;
            }
            printf("Load complete!\n");
            goto cleanup;
        }
        
        /* Loading program to device */
        printf("\nLoading program from %s (using " BOLD("%3.1f") "%% of "
               "available flash memory) -\n", options.file,
               (float)(end-start) / (rdev.mem.flash_high - rdev.mem.flash_low) * 100);
                          
        printf( BLUE("Erasing: ") );

	/* If the -e flag is specified, force erasing the whole device. Otherwise,
	 * only erase what is necessary to load the program (reduces load time
	 * slightly and allows for loading two segments of the flash separately) */
	if(options.erase)
	    rigel_erase_device(&rdev);
        else device_erase_flash(&rdev, start, (end - start) / BYTES_PER_ROW);

        printf( BLUE("Loading: ") );
        if(device_load_program(&rdev, prog, start, end) == -1) {
            rigel_error("Program load failed! Check your connection.\n");
            goto r_error;
        }
        printf("Complete!\n");
    }
    
cleanup:
    if(prog)
        rigel_program_free(&rdev, prog);
    
    if(options.run) {
        
        /* This is a hack (kind of). If we disable IFI erase extensions,
         * we must still use the IFI_RUN extension to run a program.
         * Therefore, we re-enable IFI extensions at this point if
         * we are running a program and the controller IS an IFI one. */
        if(options.noifi)
            rdev.is_ifi = was_ifi;
        
        printf("Running program and disconnecting.\n\n");
        device_run_program(&rdev);
        
        if(options.term) {
            /* We do not want the process to terminate on SIGINT (Ctrl-C)
             * so that we can gracefully clean up when the user is sick of
             * reading output from their PIC18. */
            signal(SIGINT, sigint);
            
            capture_output(&rdev, options.fterm);
        }
    } else {
        device_reset(&rdev);
        printf("Device reset (program not running); disconnecting.\n\n");
    }
    
    device_disconnect(&rdev);
    return 0;

usage:
    printf("Rigel AN851/FRC Program Reader/Loader, version " PACKAGE_VERSION ".\n"
#ifdef __GNUC__
           "Compiled with GCC "__VERSION__"\n\n"
#endif
           "Usage:\n%s [OPTIONS] [FILENAME]\nUse --help for more info.\n", argv[0]);
           
if(options.help)
printf(
   "\nOptions:\n\n"
   " -s, --serial=DEV  TTY device used for connection. Defaults to %s.\n"
   "     --read=REG    Dump memory region to HEX file. Valid: program (default),\n"
   "                   boot, eeprom.\n"
   " -f, --format=FMT  Specify the format for the input or output program.\n"
   "                   Valid: hex (INHEX32), bin (IFI BIN), raw (binary data)\n"
   " -r, --run=yes,no  Run program after all operations complete [default].\n"
   " -e, --erase       Force erase a device. Implied for program loads.\n"
   " -i, --no-ifi      Disable IFI erase extensions for IFI controllers.\n"
   " -p, --eeprom      Load binary image to data EEPROM.\n"
   " -m, --master      Load a program to the master processor (IFI ONLY).\n"
   " -c, --configreg   Print out various configuration register info.\n"
   " -l, --devlist     Use file CONF for device configuration settings.\n"
   "                   Defaults to ~/.rigelrc or /etc/rigelrc.\n"
   " -v, --verify      Verify all write operations to the device.\n"
   " -h, --help        Display this message.\n"
   " FILENAME          Filename to load program from, or dump memory to.\n\n"
   "Report bugs to <hbock@providence.edu>.\n",
   DEFAULT_SERIAL_PORT);
           
    return 0;

r_error:
    if(prog)
        rigel_program_free(&rdev, prog);
    
    device_disconnect(&rdev);
    exit(1);
}
