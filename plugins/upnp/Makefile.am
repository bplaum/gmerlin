AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = \
be_mdb_upnp.la  \
fe_mdb_upnp.la  \
fe_renderer_upnp.la

fe_renderer_upnp_la_SOURCES = fe_renderer_upnp.c
fe_renderer_upnp_la_LIBADD = @MODULE_LIBADD@

fe_mdb_upnp_la_SOURCES = fe_mdb_upnp.c
fe_mdb_upnp_la_LIBADD = @MODULE_LIBADD@

be_mdb_upnp_la_SOURCES = be_mdb_upnp.c
be_mdb_upnp_la_LIBADD = @MODULE_LIBADD@


