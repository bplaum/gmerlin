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
Desktop_DATA = gmerlin-player.desktop

bin_PROGRAMS = gmerlin


# bin_SCRIPTS = gmerlin_launcher

gmerlin_SOURCES = \
cfgdialog.c \
gmerlin.c \
main.c \
mainmenu.c \
mainwindow.c \
remote.c 

noinst_HEADERS = \
display.h \
gmerlin.h \
player_remote.h

pkgdatadir = $(datadir)/@PACKAGE@/player

skin_data = \
gmerlin-player.css

pkgdata_DATA = $(skin_data)

gmerlin_LDADD = $(top_builddir)/lib/libgmerlin.la $(top_builddir)/lib/gtk/libgmerlin_gtk.la @XML2_LIBS@ @GTK_LIBS@

EXTRA_DIST = gmerlin_launcher gmerlin-player.desktop $(skin_data)
