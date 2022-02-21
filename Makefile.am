# aclocal Path
ACLOCAL_AMFLAGS = -I m4

if HAVE_GTK
gtk_pc = gmerlin-gtk.pc
else
gtk_pc = 
endif

SUBDIRS = iconfont web lib plugins tests apps icons include m4 utils . po

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gmerlin.pc $(gtk_pc) 

pkgdatadir = $(datadir)/gmerlin/
pkgdata_DATA = plugin.sym
dist_pkgdata_DATA = plugin.sym


EXTRA_DIST= \
config.rpath \
gmerlin.pc.in \
autogen.sh \
make_potfiles \
README.gavftools

