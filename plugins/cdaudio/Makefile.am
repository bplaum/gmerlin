plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_LDFLAGS = @GMERLIN_PLUGIN_LDFLAGS@ -avoid-version -module

if HAVE_MUSICBRAINZ
musicbrainz_sources = musicbrainz.c
musicbrainz_libs = @MUSICBRAINZ_LIBS@
musicbrainz_cflags = @MUSICBRAINZ_CFLAGS@
else
musicbrainz_sources =
musicbrainz_libs =
musicbrainz_cflags = 
endif


if HAVE_LIBCDDB
cddb_sources = cddb.c
cddb_libs = @CDDB_LIBS@
cddb_cflags = @CDDB_CFLAGS@
else
cddb_sources =
cddb_libs =
cddb_cflags = 
endif


AM_CFLAGS = \
$(cddb_cflags) \
$(musicbrainz_cflags) \
@CDIO_CFLAGS@ \
@CDIO_CDDA_CFLAGS@ \
@CDIO_PARANOIA_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

plugin_LTLIBRARIES = i_cdaudio.la

i_cdaudio_la_SOURCES = \
i_cdaudio.c \
cdaudio.c \
cdaudio_xml.c \
sha1.c	\
cdparanoia.c \
cdtext.c \
$(musicbrainz_sources) \
$(cddb_sources)


i_cdaudio_la_LIBADD = \
$(musicbrainz_libs) \
$(cddb_libs) \
@CDIO_LIBS@ \
@CDIO_CDDA_LIBS@ \
@CDIO_PARANOIA_LIBS@ \
@MODULE_LIBADD@ \
@XML2_LIBS@

noinst_HEADERS = cdaudio.h sha1.h
