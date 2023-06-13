plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@UUID_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

noinst_LTLIBRARIES = \
libupnp.la

libupnp_la_SOURCES = \
client.c \
devicedesc.c \
didl.c \
event.c \
eventlistener.c \
soap.c \
ssdp.c \
upnputils.c


libupnp_la_LIBADD =  @UUID_LIBS@

