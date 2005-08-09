KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

EXTRA_CFLAGS = -I $(src)/../lapd/ -I $(src)/../visdn-core/

obj-m	:= hfc-4s.o
hfc-4s-y	:=	st_port.o		\
			st_port_sysfs.o		\
			pcm_port.o		\
			pcm_port_sysfs.o	\
			chan.o			\
			chan_sysfs.o		\
			fifo.o			\
			fsm.o			\
			card_sysfs.o		\
			hfc-4s_main.o 

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1

install:
	echo "Install not yet implemented!!!"
