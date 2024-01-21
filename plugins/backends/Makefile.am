AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

if HAVE_DBUS
dbus_plugins = 
else
dbus_plugins =
endif

plugin_LTLIBRARIES = \
be_renderer_gmerlin.la \
be_mdb_gmerlin.la

be_renderer_gmerlin_la_SOURCES = be_renderer_gmerlin.c
be_renderer_gmerlin_la_LIBADD = @MODULE_LIBADD@

be_mdb_gmerlin_la_SOURCES = be_mdb_gmerlin.c
be_mdb_gmerlin_la_LIBADD = @MODULE_LIBADD@

