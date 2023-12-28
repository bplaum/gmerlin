plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@ALSA_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"


AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module


plugin_LTLIBRARIES = oa_alsa.la

oa_alsa_la_SOURCES = oa_alsa.c alsa_common.c
oa_alsa_la_LIBADD = @ALSA_LIBS@ @MODULE_LIBADD@

noinst_HEADERS = alsa_common.h
