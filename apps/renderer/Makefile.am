plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
@DBUS_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = gmerlin-renderer

gmerlin_renderer_SOURCES = \
renderer.c \
gmerlin-renderer.c


Desktopdir = $(datadir)/applications
Desktop_DATA = gmerlin-renderer.desktop

gmerlin_renderer_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl @UUID_LIBS@ @XML2_LIBS@ @JSON_LIBS@ -lpthread

noinst_HEADERS = renderer.h

EXTRA_DIST = gmerlin-renderer.desktop
