plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = gmerlin-server

gmerlin_server_SOURCES = \
server.c \
gmerlin-server.c


gmerlin_server_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl @UUID_LIBS@ @XML2_LIBS@ @JSON_LIBS@ -lpthread

noinst_HEADERS = server.h
