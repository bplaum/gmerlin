AM_CPPFLAGS = -I$(top_srcdir)/include

plugindir=$(pkglibdir)/plugins
AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

AM_CFLAGS = \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

if HAVE_DBUS
dbus_modules = res_udisks2.la res_avahi.la
else
dbus_modules = 
endif

plugin_LTLIBRARIES = \
res_ssdp.la \
$(dbus_modules)

res_udisks2_la_CFLAGS = @DBUS_CFLAGS@ $(AM_CFLAGS)
res_udisks2_la_SOURCES = res_udisks2.c
res_udisks2_la_LIBS = @DBUS_LIBS@

res_avahi_la_CFLAGS = @DBUS_CFLAGS@ $(AM_CFLAGS)
res_avahi_la_SOURCES = res_avahi.c
res_avahi_la_LIBS = @DBUS_LIBS@

res_ssdp_la_CFLAGS = $(AM_CFLAGS)
res_ssdp_la_SOURCES = res_ssdp.c

