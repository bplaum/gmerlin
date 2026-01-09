plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = @WAYLAND_CFLAGS@ @WAYLAND_CURSOR_CFLAGS@ \
@WAYLAND_EGL_CFLAGS@ @XKB_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

noinst_HEADERS = \
  xdg-shell-client-protocol.h \
  xdg-decoration.h

plugin_LTLIBRARIES = \
ov_wayland.la

ov_wayland_la_SOURCES = ov_wayland.c xdg-shell-client-protocol.c xdg-decoration.c
ov_wayland_la_LIBADD =  @WAYLAND_LIBS@ @WAYLAND_CURSOR_LIBS@ @WAYLAND_EGL_LIBS@ @XKB_LIBS@ @MODULE_LIBADD@
