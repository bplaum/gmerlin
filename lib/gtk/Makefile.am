plugindir=$(pkglibdir)/plugins

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = \
@GTK_CFLAGS@ \
@FREETYPE_CFLAGS@ \
@FONTCONFIG_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"


libgmerlin_gtk_la_LDFLAGS = -version-info @LTVERSION_CURRENT@:@LTVERSION_REVISION@:@LTVERSION_AGE@

lib_LTLIBRARIES = libgmerlin_gtk.la
noinst_HEADERS = gtk_dialog.h mdb_private.h

libgmerlin_gtk_la_SOURCES = \
aboutwindow.c \
backendmenu.c \
configdialog.c \
dictview.c \
driveselect.c \
fileselect.c \
gtkutils.c \
logwindow.c \
mdb_list.c \
mdb_tree.c \
multiinfo.c \
textview.c \
trackinfo.c \
urlselect.c

libgmerlin_gtk_la_LIBADD = @GTK_LIBS@ $(top_builddir)/lib/libgmerlin.la @XML2_LIBS@ @LIBM@

