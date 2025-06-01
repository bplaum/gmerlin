AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\" \
@GL_CFLAGS@


plugin_LTLIBRARIES = \
  vis_glscope.la

vis_glscope_la_LIBADD = @MODULE_LIBADD@ @GL_LIBS@ @EGL_LIBS@ -lm
vis_glscope_la_SOURCES  = vis_glscope.c 

