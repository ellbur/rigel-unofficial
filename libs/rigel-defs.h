/*  This program is free software; you can redistribute it and/or modify it
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
 
#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>

typedef uint8_t  byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef uint64_t qword;

#ifdef __APPLE__
    #ifdef __BIG_ENDIAN__
    #define __ARCH_BE
    #else
    #define __ARCH_LE
    #endif
#else /* Linux, FreeBSD */
    #include <endian.h>
    #if __BYTE_ORDER == __LITTLE_ENDIAN
    #define __ARCH_LE
    #else
    #define __ARCH_BE
    #endif
#endif

#define rigel_error(...) \
    do { fprintf(stderr, "Error! " __VA_ARGS__); } while(0)

#define rigel_warn(...) \
  do { fprintf(stderr, "Warning: " __VA_ARGS__); } while(0)

#define rigel_fatal(...) \
  do { fprintf(stderr, " *** Fatal Error! " __VA_ARGS__); \
       exit(1); } while(0)

#define max(x,y) ((x) > (y) ? (x) : (y))
#define min(x,y) ((x) < (y) ? (x) : (y))

#ifdef __ARCH_LE
    #define MAKEWORD(l,h) ( (word)(((byte)(l)) | ((word)((byte)(h))) << 8) )
    #define HIWORD(x) (word)(((x) >> 16) & 0xFFFF)
    #define LOWORD(x) (word)((x) & 0xFFFF)
    #define HIBYTE(x) (byte)(((x) >> 8) & 0xFF)
    #define LOBYTE(x) (byte)((x) & 0xFF)
#else
    #define MAKEWORD(l,h) ( (word)(((byte)(h)) | ((word)((byte)(l))) << 8) )
    #define HIWORD(x) (word)((x) & 0xFFFF)
    #define LOWORD(x) (word)(((x) >> 16) & 0xFFFF)
    #define HIBYTE(x) (byte)((x) & 0xFF)
    #define LOBYTE(x) (byte)(((x) >> 8) & 0xFF)
#endif

#endif /* _TYPES_H */
