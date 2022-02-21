plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@XML2_CFLAGS@ \
@DBUS_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

bin_PROGRAMS = gmerlin-controlpanel gmerlin-controlpanel-ctl

gmerlin_controlpanel_SOURCES = \
gmerlin-controlpanel.c

gmerlin_controlpanel_ctl_SOURCES = \
gmerlin-controlpanel-ctl.c

gmerlin_controlpanel_LDADD = $(top_builddir)/lib/libgmerlin.la
gmerlin_controlpanel_ctl_LDADD = $(top_builddir)/lib/libgmerlin.la

# EXTRA_DIST = gmerlin-remote.desktop
