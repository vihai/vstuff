KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

obj-m	:= lapd.o

lapd-y	:= af_lapd.o lapd_in.o lapd_out.o lapd_dev.o \
	multiframe.o tei_mgmt.o tei_mgmt_nt.o tei_mgmt_te.o

all:	
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1 CONFIG_DEBUG_SPINLOCK=1

install:
	echo "Install not yet implemented!!!"
