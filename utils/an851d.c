/* This has to be the most ridiculous program I've ever written. */

#include "an851d.h"

#include <stdio.h>
#define __USE_XOPEN
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>

#include "pic18.h"
#include "inhex32.h"
#include "serialio.h"

/* AN851 daemon state machine */
/* The static memory of our device - flash (program), eeprom, and config */
static uint8_t *flash;
static uint8_t *eeprom;
static uint8_t *config;
static struct pic18_memory_layout limits = { 0x000800, 0x01FFFF, 0x000000, 0x0003FF, 0x300000, 0x3000FF };

/* Internal "working space" */
static uint8_t internal[INTERNAL_BUFFER_SIZE];

static int boot_mode;
static int escape;
static int synchronized;

static int fd;
static int tx_packets, rx_packets;
static int rx_errors;

static uint8_t rx_command, tx_command;
static uint16_t version, devid;

static int valid_flash(dword address, dword length);
static int valid_eeprom(dword address, dword length);
static int valid_config(dword address, dword length);
static int an851d_rd( byte cmd, dword addr, byte length, void *data );
static int internal_tx( word length );
static int process_packet( byte *data, word len );



int an851d_main( void )
{
    int len = 0;
    byte data[INTERNAL_BUFFER_SIZE * 2];
    
    sio_settimeout(0);
    while(boot_mode) {
        len = sio_read(fd, data, sizeof(data));
        if(len <= 0) continue;
        
        while(!(data[len-1] == ETX && data[len-2] != DLE)) {
            len += sio_read(fd, &data[len], sizeof(data) - len);
        }
        
        //if( (len = an851_wait_response(data, sizeof(data))) == -1 ) {
        //    rigel_warn("Error in reading serial port\n");
        //    rx_errors++;
        //}
        
            
        if(len == 0) continue;
        
        if(process_packet(data, len) == -1) {
            rx_errors++;
            rigel_warn("RX error (%d total)\n", rx_errors);
        } else rx_packets++;
        len = 0;
        fflush(stdout);
    }
    return 0;
}

static int process_packet(byte *buf, word rxlen)
{
    int i, chk = 0;
    byte length, checksum;
    dword address;
    
    /* Strip control characters from received data */
    for(i = 0, length = 0; i < rxlen; i++) {
        if(IS_CONTROL(buf[i])) {
            if(buf[i] == DLE)
                internal[length++] = buf[++i];
            else
                i++;
        } else {
            internal[length] = buf[i];
            length++;
        }
    }
        
    /* Get the packet checksum */
    for(i = 0; i < length - 1; i++)
        chk += internal[i];
    
    checksum = ((~chk + 1) & 0xFF);
    
    if( checksum != internal[length-1] ) {
        rigel_warn("rx checksum mismatch! got %02X, calculated %02X\n",
             internal[length-1], checksum);
        return -1;
    }
    
    rx_command = internal[0];
    
    /* This is safe to do, even if the packet is less than 5 bytes,
     * because our internal buffer is always INTERNAL_BUFFER_SIZE */
    length = internal[1];
    address = ADDRESS(internal[2], internal[3], internal[4]);
        
    switch(rx_command) {
    case RD_VERSION: return an851d_version();
    case RD_CONFIG: return an851d_rd_config(address, length); 
    case RD_FLASH:  return an851d_rd_flash(address, length);
    case RD_EEDATA: return an851d_rd_eeprom(address, length);
    case WR_FLASH:  return an851d_wr_flash(address, length, &internal[5]);
    case WR_EEDATA: return an851d_wr_eeprom(address, length, &internal[5]);
    case ER_FLASH:  return an851d_er_flash(address, length);
    case IFI_WR_ROW: return ifi_wr_row(address, length, internal[5]);
    case IFI_RUN_CODE:
        printf("IFI_RUN_CODE (user disconnect?)\n");
        return 0;
            
    default:
        printf("RESET [%02X]\n", rx_command);
        rx_errors = 0;
        rx_packets = tx_packets = 0;
        
        return 0;
    }
}
        
    
/* Initialize the state machine, open ports, and start the daemon */
int an851d_initialize( const char *port, const char *inithex )
{
    #if 0
    if( (fd = sio_open(port)) == -1) {
        rigel_error("Initializing TTY devices\n");
        return -1;
    }
    #endif

    if((fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1) {
	rigel_error("Initializing pseudo-tty device: %s\n", strerror(errno));
	return -1;
    }

    if(grantpt(fd) == -1) {
	rigel_error("Granting permissions to pseudo-tty device: %s\n", strerror(errno));
	return -1;
    }

    unlockpt(fd);

    printf("Successfully created slave pseudo-terminal device %s! Pass this to rigel -s\n",
	   ptsname(fd));
    
    devid = AN851D_DEVID;
    version = AN851D_VERSION;

    flash  = (uint8_t *)malloc( limits.flash_high );
    eeprom = (uint8_t *)malloc( limits.eeprom_high );
    config = (uint8_t *)malloc( limits.config_high - limits.config_low );
    
    memset(internal, 0, INTERNAL_BUFFER_SIZE);    
    memset(eeprom, 0xFF, limits.eeprom_high);
    memset(flash,  0x00, limits.flash_high);
    
    synchronized = escape = 0;
    tx_packets = rx_packets = 0;
    rx_errors = 0;
    boot_mode = 1;
    
    sio_settimeout( 0 );
    return an851d_main();
}

int ifi_wr_row(uint32_t address, uint8_t rows, uint8_t val)
{
    printf("IFI_WR_ROW 0x%06X, %d rows (0x%06X bytes), value 0x%02X\n",
           address, rows, rows * BYTES_PER_ROW, val);
    memset(&flash[address], val, rows * BYTES_PER_ROW);
    internal[0] = IFI_WR_ROW;
    
    return internal_tx(1);
}

int an851d_er_flash(uint32_t address, uint8_t blocks)
{
    uint32_t bytes = blocks * BYTES_PER_BLOCK;
    
    printf("ER_FLASH 0x%06X, %d blocks (0x%06X bytes)\n", address, blocks, bytes);
    if(address < limits.flash_low || address + bytes > limits.flash_high) {
        rigel_warn("Invalid flash erase req to address %06X of length %d\n.",
             address, bytes);
        return -1;
    }
    
    memset(&flash[address], 0xFF, bytes);
    internal[0] = ER_FLASH;
    
    return internal_tx(1);
}

int an851d_rd_flash(dword address, byte length)
{
    printf("RD_FLASH 0x%06X, 0x%02X bytes\n", address, length);
    if( address != DEVID_ADDR && address + length > limits.flash_high ) {
        rigel_warn("Invalid flash read req to address %06X of length %d.\n",
             address, length);
        return -1;
    }
    
    if(address == DEVID_ADDR)
        return an851d_rd(RD_FLASH, address, 2, &devid);

    return an851d_rd(RD_FLASH, address, length, &flash[address]);
}

int an851d_rd_config(dword address, byte length)
{
    printf("RD_CONFIG 0x%06X, %02X bytes\n", address, length);
    if( !valid_config(address, length) ) {
        rigel_warn("Invalid configuration read req to address %06X of length %d.\n",
             address, length);
        return -1;
    }
    
    return an851d_rd(RD_CONFIG, address, length, &config[address - limits.config_low]);
}

int an851d_rd_eeprom(dword address, byte length)
{
    printf("RD_EEDATA 0x%06X, %02X bytes\n", address, length);
    if( !valid_eeprom(address, length) ) {
        rigel_warn("Invalid EEPROM read req to address %06X of length %d.\n",
             address, length);
        return -1;
    }
    
    return an851d_rd(RD_EEDATA, address, length, &eeprom[address]);
}

int an851d_wr_eeprom(word address, byte length, void *data)
{
    printf("WR_EEDATA 0x%06X, %02X bytes\n", address, length);
    memcpy(&eeprom[address], data, length);
    
    internal[0] = WR_EEDATA;
    return internal_tx(1);
}

int an851d_wr_flash(dword address, byte blocks, void *data)
{
    printf("WR_FLASH 0x%06X, %d blocks (0x%04X bytes)\n",
           address, blocks, blocks*BYTES_PER_BLOCK);
    if( !valid_flash(address, blocks*BYTES_PER_BLOCK) ) {
        rigel_warn("Invalid flash write req to address %06X of %d blocks\n.",
		   address, blocks);
    }
    
    memcpy(&flash[address], data, blocks * BYTES_PER_BLOCK);
    
    internal[0] = WR_FLASH;
    return internal_tx(1);
}

int an851d_version( void )
{
    printf("RD_VERSION\n");
    
    internal[0] = RD_VERSION;
    internal[1] = 0x02;
    internal[2] = LOBYTE(version);
    internal[3] = HIBYTE(version);
    
    return internal_tx(4);
}

static int valid_flash(dword address, dword length)
{
    return !(address < limits.flash_low || 
            (address + length) > limits.flash_high) ||
            (address == DEVID_ADDR);
}
static int valid_config(dword address, dword length)
{
    return !(address < limits.config_low || 
            (address + length) > limits.config_high);
}
static int valid_eeprom(dword address, dword length)
{
    return !( (address < limits.eeprom_low || 
              (address + length) > limits.eeprom_high) &&
              length <= MAX_DATA_LENGTH );
}

static int an851d_rd( byte cmd, dword addr, byte length, void *data )
{
    internal[0] = cmd;
    internal[1] = length;
    internal[2] = ADDRL(addr);
    internal[3] = ADDRH(addr);
    internal[4] = ADDRU(addr);
    memcpy(&internal[5], data, length);
    
    return internal_tx( length + 5 );
}

static int internal_tx( word length )
{
    int i, c, chk = 0;
    byte checksum;
    static byte buffer[INTERNAL_BUFFER_SIZE * 2] = { STX, STX };
     
    tx_command = internal[0];
    
    /* Get the packet checksum */
    for(c = 0; c < length; c++)
        chk += internal[c];
    checksum = ((~chk + 1) & 0xFF);
    
    /* Escape all control characters within the data as we copy */
    for(i = 0, c = 2; i < length; i++, c++) {
        if(IS_CONTROL(internal[i]))
            buffer[c++] = DLE;
        buffer[c] = internal[i];
    }
    length = c;
    
    /* Insert the checksum and the end control. */
    buffer[length++] = checksum;
    buffer[length++] = ETX;
    
    tx_packets++;
        
    return sio_write(fd, buffer, length);
}

void cleanup( int sig )
{
    printf("Cleanup up on user termination.\n");
    sio_close(fd);
    free(flash);
    free(config);
    free(eeprom);
    
    exit(0);
}
static struct option anopts[] = {
    { "hex",      required_argument, NULL, 'x' },
    { "serial",   required_argument, NULL, 's' },
    { "devid",    required_argument, NULL, 'd' },
    { "version",  required_argument, NULL, 'v' },
    { "devlist",  required_argument, NULL, 'l' },
    { "no-ifi",   no_argument,       NULL, 'i' },
    { "help",     no_argument,       NULL, 'h' },
    { NULL,       0,                 NULL, 0   }
};
int main( int argc, char **argv )
{
#if 0
    int c;
    while((c = getopt_long(argc, argv, "x:d:v:l:ih",
           longopts, NULL)) != -1) {
        switch (c) {
            case 'x':
                printf("Loading with initial HEX file: %s\n", optarg);
#endif

    signal(SIGINT, cleanup);
    an851d_initialize( argv[1], NULL );
    return 0;
}
