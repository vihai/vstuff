KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

EXTRA_CFLAGS = -I $(src)/../lapd/

obj-m	:= fake_isdn_main.o
fake_isdn-y	:= fake_isdn_main.o

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1
