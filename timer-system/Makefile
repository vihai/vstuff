KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

EXTRA_CFLAGS = -I $(src)/../visdn-core/

obj-m	:= visdn-timer-system.o
visdn-timer-system-y	:= timer_system_main.o

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1

install:
	echo "Install not yet implemented!!!"
