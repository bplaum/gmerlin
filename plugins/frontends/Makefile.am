AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

if HAVE_NCURSES
ncurses_libraries = fe_renderer_ncurses.la
endif

plugin_LTLIBRARIES = \
fe_renderer_gmerlin.la \
fe_mdb_gmerlin.la \
fe_renderer_console.la \
$(ncurses_libraries)

fe_renderer_gmerlin_la_SOURCES = fe_renderer_gmerlin.c
fe_renderer_gmerlin_la_LIBADD = @MODULE_LIBADD@

fe_renderer_console_la_SOURCES = fe_renderer_console.c
fe_renderer_console_la_LIBADD = @MODULE_LIBADD@

fe_renderer_ncurses_la_SOURCES = fe_renderer_ncurses.c
fe_renderer_ncurses_la_CFLAGS = $(AM_CFLAGS) @NCURSES_CFLAGS@
fe_renderer_ncurses_la_LIBADD = @MODULE_LIBADD@ @NCURSES_LIBS@


fe_mdb_gmerlin_la_SOURCES = fe_mdb_gmerlin.c
fe_mdb_gmerlin_la_LIBADD = @MODULE_LIBADD@
