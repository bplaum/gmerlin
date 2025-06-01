plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = @X_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"


AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module


plugin_LTLIBRARIES = \
i_x11.la \
ov_x11.la

ov_x11_la_SOURCES = ov_x11_new.c
ov_x11_la_LIBADD =  @X_LIBS@ @MODULE_LIBADD@

i_x11_la_SOURCES = i_x11.c grab.c
i_x11_la_LIBADD =  @X_LIBS@ @XFIXES_LIBS@ @MODULE_LIBADD@

noinst_HEADERS = 

