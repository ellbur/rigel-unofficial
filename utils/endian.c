#include <stdio.h>
#include <types.h>
#include <inhex32.h>

#define A 0x12345678
#define B 0xAAAABBBB

int main()
{
    uint32_t x = 0xFFFF;

    SET_EXT_LINEAR(x, 0x01);
    printf("%06X\n", x);

    x = 0xFFFF;
    ((x) |= ((0x01) >> 16) & 0x0000FFFF);

    printf("%06X\n", x);
    
    return 0;
}
