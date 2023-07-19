plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = gmerlin-record

dist_man_MANS =
manpages = gmerlin-record.1
if CROSSBUILD
else
dist_man_MANS += $(manpages)
endif
#CLEANFILES = $(manpages)

gmerlin-record.1: gmerlin-record
	$(builddir)/gmerlin-record -help-man > $(builddir)/gmerlin-record.1


gmerlin_record_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl
gmerlin_record_SOURCES=gmerlin-record.c
