AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = \
i_v4l2.la \
ov_v4l2.la \
c_v4l2.la \
res_v4l2.la

i_v4l2_la_SOURCES  = i_v4l2.c
i_v4l2_la_LIBADD  = @MODULE_LIBADD@ $(convert_libs)

ov_v4l2_la_SOURCES  = ov_v4l2.c v4l2_common.c
ov_v4l2_la_LIBADD  = @MODULE_LIBADD@ $(convert_libs)

c_v4l2_la_SOURCES  = decode_p207.c decode.c
c_v4l2_la_LIBADD  = @MODULE_LIBADD@ $(convert_libs)

res_v4l2_la_SOURCES  = res_v4l2.c
res_v4l2_la_LIBADD  = @MODULE_LIBADD@ @UDEV_LIBS@

noinst_HEADERS = v4l2_common.h convert.h decode.h

