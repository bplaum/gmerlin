plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = gmerlin-play

man_MANS =
manpages = gmerlin-play.1
if CROSSBUILD
else
man_MANS += $(manpages)
endif
CLEANFILES = $(manpages)


gmerlin-play.1: gmerlin-play
	$(builddir)/gmerlin-play -help-man > $(builddir)/gmerlin-play.1

gmerlin_play_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl
