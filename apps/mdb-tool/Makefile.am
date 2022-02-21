plugindir=$(pkglibdir)/plugins

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = mdb-tool
man_MANS = mdb-tool.1

CLEANFILES = $(man_MANS)


mdb-tool.1: mdb-tool
	$(builddir)/mdb-tool -help-man > $(builddir)/mdb-tool.1

mdb_tool_LDADD = $(top_builddir)/lib/libgmerlin.la -ldl


