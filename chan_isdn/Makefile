

.EXPORT_ALL_VARIABLES:

INSTALL_PREFIX=
ASTERISK_HEADER_DIR=$(INSTALL_PREFIX)/usr/include

MODULES_DIR=$(INSTALL_PREFIX)/usr/lib/asterisk/modules

PROC=$(shell uname -m)

DEBUG=-g #-pg
INCLUDE=-I$(ASTERISK_HEADER_DIR)
CFLAGS=-pipe -Wall -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE
CFLAGS+=-O6
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
CFLAGS+=$(shell if uname -m | grep -q ppc; then echo "-fsigned-char"; fi)



LIBS=-ldl -lpthread -lm
CC=gcc
INSTALL=install

SHAREDOS=chan_isdn.so 

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations

CFLAGS+=-DCRYPTO

all: $(SHAREDOS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

chan_isdn.so: chan_isdn.o
	$(CC) -shared -Xlinker -x -o $@ chan_isdn.o

install: all
	for x in $(SHAREDOS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

config: all
	cp isdn.conf $(INSTALL_PREFIX)/etc/asterisk/

