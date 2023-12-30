AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

if HAVE_DBUS
dbus_modules = res_udisks2.la
else
dbus_modules = 
endif

plugin_LTLIBRARIES = \
$(dbus_modules)



res_udisks2_la_CFLAGS = @DBUS_CFLAGS@ $(AM_CFLAGS)
res_udisks2_la_SOURCES = res_udisks2.c
res_udisks2_la_LIBS = @DBUS_LIBS@

