AM_CFLAGS = -g -Wall -DSYSCONFDIR=\"@sysconfdir@\"

lib_LIBRARIES = librigel.a

pkginclude_HEADERS =\
	rigel-defs.h \
	serialio.h \
	pic18.h \
	inhex32.h \
	device.h \
	an851.h

librigel_a_SOURCES =\
	device.c \
	inhex32.c \
	an851.c \
	serialio.c