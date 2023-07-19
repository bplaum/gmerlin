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

bin_PROGRAMS = gmerlin-playerproxy

#manpages = gmerlin-playerproxy.1
dist_man_MANS =
if CROSSBUILD
else
dist_man_MANS += $(manpages)
endif
#CLEANFILES = $(manpages)

gmerlin_playerproxy_SOURCES = \
gmerlin-playerproxy.c

gmerlin_playerproxy_LDADD = $(top_builddir)/lib/libgmerlin.la

# EXTRA_DIST = gmerlin-remote.desktop
