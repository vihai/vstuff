
bin_PROGRAMS = user network test

user_SOURCES = user.c q931.c q931_mt.c q931_ie.c \
	q931_ie_bearercap.c q931_ie_cdpn.c q931_ie_cgpn.c \
	q931_ie_chanid.c q931_ie_progind.c q931_ie_cause.c

network_SOURCES = network.c q931.c q931_mt.c q931_ie.c \
	q931_ie_bearercap.c q931_ie_cdpn.c q931_ie_cgpn.c \
	q931_ie_chanid.c q931_ie_progind.c q931_ie_cause.c

test_SOURCES = test.c

INCLUDES = -I$(srcdir)/../../lapd/

AM_CFLAGS=-Wall

