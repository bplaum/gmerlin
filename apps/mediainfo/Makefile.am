plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = gmerlin-mediainfo

manpages = gmerlin-mediainfo.1
man_MANS =
if CROSSBUILD
else
man_MANS += $(manpages)
endif
CLEANFILES = $(manpages)


gmerlin-mediainfo.1: gmerlin-mediainfo
	$(builddir)/gmerlin-mediainfo -help-man > $(builddir)/gmerlin-mediainfo.1

gmerlin_mediainfo_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl



