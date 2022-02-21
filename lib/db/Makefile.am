plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@UUID_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

noinst_LTLIBRARIES = \
libdb.la

libdb_la_SOURCES = \
audioalbum.c \
audio.c \
db.c \
dir.c \
file.c \
image.c \
object.c \
playlist.c \
root.c \
rootfolder.c \
scan.c \
stringcache.c \
thumbnail.c \
url.c \
vfolder.c \
video.c \
videocontainer.c \
videoinfo.c

