AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS =  \
@PIPEWIRE_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = \
i_pipewire.la \
res_pipewire.la

res_pipewire_la_SOURCES = res_pipewire.c
res_pipewire_la_LIBADD = @PIPEWIRE_LIBS@ @MODULE_LIBADD@

i_pipewire_la_SOURCES = i_pipewire.c pipewire_common.c
i_pipewire_la_LIBADD = @PIPEWIRE_LIBS@ @MODULE_LIBADD@

noinst_HEADERS = pipewire_common.h
