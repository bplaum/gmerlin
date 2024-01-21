AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = \
fe_renderer_gmerlin.la \
fe_mdb_gmerlin.la

fe_renderer_gmerlin_la_SOURCES = fe_renderer_gmerlin.c
fe_renderer_gmerlin_la_LIBADD = @MODULE_LIBADD@

fe_mdb_gmerlin_la_SOURCES = fe_mdb_gmerlin.c
fe_mdb_gmerlin_la_LIBADD = @MODULE_LIBADD@
