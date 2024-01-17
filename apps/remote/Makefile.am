plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@XML2_CFLAGS@ \
@DBUS_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

Desktopdir = $(datadir)/applications
Desktop_DATA = gmerlin-remote.desktop

bin_PROGRAMS = gmerlin-remote


gmerlin_remote_SOURCES = \
gmerlin-remote.c

gmerlin_remote_LDADD = $(top_builddir)/lib/libgmerlin.la @UUID_LIBS@

EXTRA_DIST = gmerlin-remote.desktop
