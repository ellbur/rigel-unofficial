#ifndef _PIC18_H
#define _PIC18_H

#include <stdint.h>

#define FLASH_BLOCK_SIZE 0x4000

#define BYTES_PER_ROW   0x40
#define BYTES_PER_BLOCK 0x08

#define PIC18_ALIGN_TO_BLOCK(l) ((l) + (BYTES_PER_BLOCK - ((l) % BYTES_PER_BLOCK)))
#define PIC18_ALIGN_TO_ROW(l) ((l) + (BYTES_PER_ROW - ((l) % BYTES_PER_ROW)))

struct pic18_memory_layout {
   uint32_t flash_low,  flash_high;
   uint32_t eeprom_low, eeprom_high;
   uint32_t config_low, config_high;
};
    
typedef struct pic18_config_registers {
    uint8_t C1L, C1H; /* CONFIG1L unused (reads 0x00) */
    uint8_t C2L, C2H;
    uint8_t C3L, C3H;
    uint8_t C4L, C4H; /* CONFIG4H unused (reads 0x00) */
    uint8_t C5L, C5H;
    uint8_t C6L, C6H;
    uint8_t C7L, C7H;
} pic18_config_t;

/* CONFIG2H */
#define WDTPS 0x1E
#define WDTEN 0x01

/* CONFIG3L */
#define BW  0x40
#define ABW 0x30
#define PM  0x03

/* CONFIG4L */
#define DEBUG  0x80
#define XINST  0x40
#define BBSIZ  0x30
#define STVREN 0x01

/* CONFIG5L */
#define CP(x) (0x01 << (x))

/* CONFIG5H */
#define CPD 0x80
#define CPB 0x40

/* CONFIG6L */
#define WRT(x) (0x01 << (x))

/* CONFIG6H */
#define WRTD 0x80
#define WRTB 0x40
#define WRTC 0x20

/* CONFIG7L */
#define EBRT(x) (0x01 << (x))

#endif /* _PIC18_H */
