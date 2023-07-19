plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

noinst_LTLIBRARIES = \
libgavftools.la

libgavftools_la_SOURCES = \
gavftools.c

noinst_HEADERS = gavftools.h gavf-decode.h gavf-server.h

AM_CPPFLAGS = -I$(top_srcdir)/include

LDADD   = libgavftools.la $(top_builddir)/lib/libgmerlin.la -ldl @XML2_LIBS@
AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

bin_PROGRAMS = \
gavf-read \
gavf-demux \
gavf-encode \
gavf-filter \
gavf-info \
gavf-mux \
gavf-recompress \
gavf-tee \
gavf-visualize

manpages = \
gavf-read.1 \
gavf-demux.1 \
gavf-encode.1 \
gavf-filter.1 \
gavf-info.1 \
gavf-mux.1 \
gavf-recompress.1 \
gavf-tee.1 \
gavf-visualize.1

man_MANS =
if CROSSBUILD
else
man_MANS += $(manpages)
endif
CLEANFILES = $(manpages)

%.1: %
	$(builddir)/$< -help-man > $(builddir)/$@


