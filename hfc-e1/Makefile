KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

EXTRA_CFLAGS = -I $(src)/../lapd/ -I $(src)/../visdn-core/ -DDEBUG

obj-m	:= hfc-e1.o
hfc-e1-y	:=	e1_port.o		\
			e1t_port_sysfs.o		\
			pcm_port.o		\
			pcm_port_sysfs.o	\
			chan.o			\
			chan_sysfs.o		\
			fifo.o			\
			fsm.o			\
			card_sysfs.o		\
			hfc-e1_main.o 

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` CONFIG_DEBUG_INFO=1

install:
	echo "Install not yet implemented!!!"
