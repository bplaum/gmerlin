AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = \
fe_mpris.la

# Disabled detection of Mris players unless we can handle them
# res_mpris_la_CFLAGS = @DBUS_CFLAGS@ $(AM_CFLAGS)
# res_mpris_la_SOURCES = res_mpris.c
# res_mpris_la_LIBS = @DBUS_LIBS@

fe_mpris_la_CFLAGS = @DBUS_CFLAGS@ $(AM_CFLAGS)
fe_mpris_la_SOURCES = fe_mpris.c
fe_mpris_la_LIBS = @DBUS_LIBS@

noinst_HEADERS = mpris.h
