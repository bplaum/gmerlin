AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = ir_gavl.la iw_gavl.la

ir_gavl_la_SOURCES = ir_gavl.c
ir_gavl_la_LIBADD = @MODULE_LIBADD@

iw_gavl_la_SOURCES = iw_gavl.c
iw_gavl_la_LIBADD = @MODULE_LIBADD@

