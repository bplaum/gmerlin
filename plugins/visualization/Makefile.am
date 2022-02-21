AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

if HAVE_GLX
glx_subdirs = glscope
else
glx_subdirs =
endif


SUBDIRS = $(glx_subdirs)

plugin_LTLIBRARIES = \
vis_cover.la


# vis_scope_la_LIBADD = @MODULE_LIBADD@ -lm
# vis_scope_la_SOURCES  = vis_scope.c 

vis_cover_la_LIBADD = @MODULE_LIBADD@ -lm
vis_cover_la_SOURCES  = vis_cover.c 

