plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@XML2_CFLAGS@ \
@GTK_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

Desktopdir = $(datadir)/applications
Desktop_DATA = gmerlin-transcoder.desktop

bin_PROGRAMS = gmerlin-transcoder


gmerlin_transcoder_SOURCES = \
main.c \
transcoder_window.c \
trackdialog.c \
tracklist.c


gmerlin_transcoder_LDADD = \
$(top_builddir)/lib/gtk/libgmerlin_gtk.la \
$(top_builddir)/lib/libgmerlin.la @GTK_LIBS@


noinst_HEADERS = \
transcoder_window.h \
tracklist.h \
trackdialog.h

EXTRA_DIST = gmerlin-transcoder.desktop
