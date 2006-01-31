AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

astmod_LTLIBRARIES = app_visdn_ppp.la
app_visdn_ppp_la_SOURCES = app_visdn_ppp.c

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/chan_visdn/		\
	-I$(astincdir)

app_visdn_ppp_la_LIBADD = 
app_visdn_ppp_la_LDFLAGS = -module -avoid-version -no-undefined
