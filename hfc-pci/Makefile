KSOURCE = /usr/src/linux

ifndef src
	src=$(shell pwd)
endif

EXTRA_CFLAGS = -I $(src)/../lapd/

obj-m	:= hfc-pci.o
hfc-pci-y	:= hfc-pci_main.o fifo.o

all:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd`

clean:
	$(MAKE) -C $(KSOURCE) clean SUBDIRS=`pwd`

debug:
	$(MAKE) -C $(KSOURCE) modules SUBDIRS=`pwd` EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDEBUG" CONFIG_DEBUG_INFO=1
