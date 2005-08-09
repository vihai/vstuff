KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

obj-m	:= visdn-core.o
visdn-core-y	:= core_main.o chan.o port.o timer.o

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

pre11:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DNO_CLASS_DEV_DEVT"

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1

install:
	echo "Install not yet implemented!!!"
