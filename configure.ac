
AC_INIT([gmerlin], [2.0.0pre1])
AC_CONFIG_SRCDIR([include/gmerlin/player.h])

AM_INIT_AUTOMAKE

AC_CONFIG_HEADERS(include/config.h)

BG_VERSION=$VERSION

BG_VERSION_MAJOR=`echo $VERSION | cut -d . -f 1`
BG_VERSION_MINOR=`echo $VERSION | cut -d . -f 2`
BG_VERSION_MICRO=`echo $VERSION | cut -d . -f 3 | cut -d p -f 1`

BUILD_ID=`date +%Y-%m-%d-%H-%M`

LTVERSION_CURRENT="3"
LTVERSION_REVISION="0"
LTVERSION_AGE="0"

AC_SUBST(BG_VERSION)
AC_SUBST(BG_VERSION_MAJOR)
AC_SUBST(BG_VERSION_MINOR)
AC_SUBST(BG_VERSION_MICRO)
AC_SUBST(LTVERSION_CURRENT)
AC_SUBST(LTVERSION_REVISION)
AC_SUBST(LTVERSION_AGE)
AC_SUBST(BUILD_ID)

SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

AC_DISABLE_STATIC
dnl AM_DISABLE_STATIC

AC_LANG(C)
LT_INIT

AM_CONDITIONAL(CROSSBUILD, [test "x${cross_compiling}" = "xyes"])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

dnl
dnl General features
dnl

AC_CHECK_SIZEOF(long)

AC_CHECK_SIZEOF(float)
AC_CHECK_SIZEOF(double)

AC_CHECK_FUNCS(vasprintf isatty)
AC_CHECK_FUNCS(canonicalize_file_name)

AC_C_BIGENDIAN(,,AC_MSG_ERROR("Cannot detect endianess"))

AC_CHECK_HEADERS([sys/select.h sys/sendfile.h ifaddrs.h])

AC_CHECK_DECLS([MSG_NOSIGNAL, SO_NOSIGPIPE],,,
               [#include <sys/types.h>
	       #include <sys/socket.h>])

dnl
dnl Linux dma-buf
dnl 

AH_TEMPLATE([HAVE_DRM], [Is DRM available?])


AC_CHECK_HEADERS([drm/drm_fourcc.h libdrm/drm_fourcc.h], have_drm="true")

if test "x$have_drm" = "xtrue"; then
AC_DEFINE([HAVE_DRM])
fi

dnl
dnl Gettext
dnl

AM_GNU_GETTEXT([external])


dnl
dnl texinfo
dnl

test_texinfo="true"
have_texinfo="false"

AC_ARG_WITH(texinfo,
    AS_HELP_STRING([--without-texinfo],
    [disable texinfo documentation generation]),
    test_texinfo="false")

if test "x$test_texinfo" = "xtrue"; then
  AC_CHECK_PROG(TEXINFO, makeinfo, "makeinfo")
  if test "x$TEXINFO" != "x"; then
    have_texinfo="true"
  fi
fi

AM_CONDITIONAL(HAVE_TEXINFO, test "x$have_texinfo" = "xtrue")




dnl
dnl Linker flags
dnl

GMERLIN_CHECK_LDFLAGS


dnl
dnl Check for Libraries
dnl

dnl Required

LT_LIB_M

XML2_REQUIRED="2.4.0"
PKG_CHECK_MODULES(XML2, libxml-2.0 >= $XML2_REQUIRED, , AC_MSG_ERROR("libxml2 not found"))
AC_SUBST(XML2_REQUIRED)

GAVL_REQUIRED="2.0.0"
PKG_CHECK_MODULES(GAVL, gavl >= $GAVL_REQUIRED, , AC_MSG_ERROR("gavl not found"))
AC_SUBST(GAVL_REQUIRED)


AH_TEMPLATE([HAVE_DBUS],
            [Do we have dbus installed?])

DBUS_REQUIRED="1.2.0"

have_dbus=false
test_dbus=true
AC_ARG_ENABLE(dbus,
              AS_HELP_STRING(--disable-dbus, [Disable DBUS support (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_dbus=true ;;
                 no) test_dbus=false ;;
               esac],
	       test_dbus=true)

if test x$test_dbus = "xtrue"; then
PKG_CHECK_MODULES(DBUS, dbus-1 >= $DBUS_REQUIRED, have_dbus=true, have_dbus=false)
fi

AC_SUBST(DBUS_REQUIRED)

AM_CONDITIONAL(HAVE_DBUS, test x$have_dbus = xtrue)

if test x$have_dbus = xtrue; then
AC_DEFINE(HAVE_DBUS)
fi

FONTCONFIG_REQUIRED="2.2.3"
PKG_CHECK_MODULES(FONTCONFIG, fontconfig >= $FONTCONFIG_REQUIRED, , AC_MSG_ERROR("fontconfig not found"))

FREETYPE_REQUIRED="9.7.3"
PKG_CHECK_MODULES(FREETYPE, freetype2 >= $FREETYPE_REQUIRED, , AC_MSG_ERROR("freetype2 not found"))

SQLITE3_LIBS=""
AC_CHECK_LIB(sqlite3, sqlite3_exec, [SQLITE3_LIBS="-lsqlite3"], AC_MSG_ERROR("sqlite not found"))
AC_SUBST(SQLITE3_LIBS)

PKG_CHECK_MODULES(UUID, uuid, , AC_MSG_ERROR("libuuid not found"))

MQ_LIBS=""
AC_CHECK_LIB(rt, mq_open, MQ_LIBS="-lrt")
AC_SUBST(MQ_LIBS)

JSON_REQUIRED="0.11.0"
PKG_CHECK_MODULES(JSON, json-c >= $JSON_REQUIRED, , AC_MSG_ERROR("json not found"))
AC_SUBST(JSON_REQUIRED)

AH_TEMPLATE([HAVE_MQ], [POSIX message queues])

AM_CONDITIONAL(HAVE_MQ, test "x$MQ_LIBS" != "x")
if test "x$MQ_LIBS" != "x"; then
  AC_DEFINE(HAVE_MQ)
fi

dnl
dnl Cairo & Pango
dnl

PKG_CHECK_MODULES(CAIRO, cairo, , AC_MSG_ERROR("cairo not found"))
PKG_CHECK_MODULES(PANGO, pango, , AC_MSG_ERROR("pango not found"))
PKG_CHECK_MODULES(PANGOCAIRO, pangocairo, , AC_MSG_ERROR("pangocairo not found"))

dnl
dnl libudev
dnl

PKG_CHECK_MODULES(UDEV, libudev, , AC_MSG_ERROR("libudev not found"))


dnl
dnl Gtk
dnl

GTK_REQUIRED="3.10.8"

PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQUIRED, have_gtk=true, have_gtk=false)
AM_CONDITIONAL(HAVE_GTK, test x$have_gtk = xtrue)
AC_SUBST(GTK_REQUIRED)
dnl Always be in sync with the newest gtk
GTK_CFLAGS="$GTK_CFLAGS -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED"


dnl
dnl X11
dnl

GMERLIN_CHECK_X11

dnl
dnl OpenGL
dnl

GMERLIN_CHECK_OPENGL

dnl
dnl Libva
dnl

GMERLIN_CHECK_LIBVA

dnl
dnl Pulseaudio
dnl

GMERLIN_CHECK_PULSEAUDIO

dnl
dnl pipewire
dnl

dnl GMERLIN_CHECK_PIPEWIRE


dnl
dnl Xinerama
dnl

AH_TEMPLATE([HAVE_LIBXINERAMA],
            [Do we have xinerama installed?])

have_xinerama="false"
XINERAMA_LIBS=""

if test x$have_x = xtrue; then

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS

CFLAGS=$X_FLAGS
LIBS="$X_LIBS -lXinerama"

AC_MSG_CHECKING(for xinerama)
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <X11/Xlib.h>
		#include <X11/extensions/Xinerama.h>
		int main()
		{
                int i = 0;
                /* We ensure the function is here but never call it */
                if(i)
                  XineramaQueryExtension(NULL, NULL, NULL);
                return 0;
		}
		]])],
            [XINERAMA_LIBS="-lXinerama";have_xinerama=true;AC_MSG_RESULT(Yes)],
	    AC_MSG_RESULT(No))

if test x$have_xinerama = "xtrue"; then
AC_DEFINE(HAVE_LIBXINERAMA)
fi

AC_SUBST(XINERAMA_LIBS)

CFLAGS=$OLD_CFLAGS
LIBS=$OLD_LIBS

fi

dnl
dnl XTest
dnl

AH_TEMPLATE([HAVE_XTEST],
            [Do we have xtest installed?])

have_xtest="false"
XTEST_LIBS=""

if test x$have_x = xtrue; then

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS
                                                                               
CFLAGS=$X_FLAGS
LIBS="$X_LIBS -lXtst"

AC_MSG_CHECKING(for xtest)
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <X11/Xlib.h>
		#include <X11/extensions/XTest.h>
		int main()
		{
		int i = 0;
		 /* We ensure the function is here but never call it */
		 if(i)
		   XTestQueryExtension(NULL, NULL, NULL, NULL, NULL);
		 return 0;
		 }]])],
		[XTEST_LIBS="-lXtst";have_xtest=true;AC_MSG_RESULT(Yes)],
		AC_MSG_RESULT(No))

if test x$have_xtest = "xtrue"; then
AC_DEFINE(HAVE_XTEST)
fi

AC_SUBST(XTEST_LIBS)

CFLAGS=$OLD_CFLAGS
LIBS=$OLD_LIBS

fi

AM_CONDITIONAL(HAVE_XTEST, test x$have_xtest = xtrue)

dnl
dnl X11 DPMS extension
dnl

AH_TEMPLATE([HAVE_XDPMS],
            [Do we have X11 dpms extension installed?])

have_xdpms="false"
XDPMS_LIBS=""

if test x$have_x = xtrue; then

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS
CFLAGS=$X_FLAGS
LIBS="$X_LIBS -lXext"

AC_MSG_CHECKING(for x11 dpms)
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <X11/Xlib.h>
		#include <X11/extensions/dpms.h>
		int main()
		{
		int i = 0;
		/* We ensure the function is here but never call it */
		if(i)
		  DPMSQueryExtension(NULL, NULL, NULL);
		return 0;
		} 
		]])],
		[XDPMS_LIBS="-lXext";have_xdpms=true;AC_MSG_RESULT(Yes)],
		AC_MSG_RESULT("No"))
	       
if test x$have_xdpms = "xtrue"; then
AC_DEFINE(HAVE_XDPMS)
fi

AC_SUBST(XDPMS_LIBS)

CFLAGS=$OLD_CFLAGS
LIBS=$OLD_LIBS

fi

dnl
dnl XFixes
dnl

AH_TEMPLATE([HAVE_XFIXES],
            [Do we have XFixes extension installed?])

have_xfixes="false"
XFIXES_LIBS=""

if test x$have_x = xtrue; then

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS
CFLAGS=$X_FLAGS
LIBS="$X_LIBS -lXfixes"

AC_MSG_CHECKING(for x11 Xfixes)
AC_LINK_IFELSE([AC_LANG_SOURCE(
		    [[#include <X11/Xlib.h>
		      #include <X11/extensions/Xfixes.h>
		      int main()
		      {
			  int i = 0;
			  /* We ensure the function is here but never call it */
			  if(i)
			    XFixesQueryExtension(NULL, NULL, NULL);
			    return 0;
		      }
		     ]])],
               [XFIXES_LIBS="-lXfixes";have_xfixes=true;AC_MSG_RESULT(Yes)],
	       AC_MSG_RESULT("No"))

if test x$have_xfixes = "xtrue"; then
AC_DEFINE(HAVE_XFIXES)
fi

AC_SUBST(XFIXES_LIBS)

CFLAGS=$OLD_CFLAGS
LIBS=$OLD_LIBS

fi

dnl
dnl ncurses
dnl

AH_TEMPLATE([HAVE_NCURSES],
            [Do we have ncurses installed?])

dnl PKG_CHECK_MODULES(NCURSES, ncurses, have_ncurses="true", have_ncurses="false")
PKG_CHECK_MODULES(NCURSES, ncursesw, have_ncurses="true", have_ncurses="false")

if test x$have_ncurses = "xtrue"; then
AC_DEFINE(HAVE_NCURSES)
fi

AM_CONDITIONAL(HAVE_NCURSES, test x$have_ncurses = xtrue)

dnl
dnl libexif
dnl

AH_TEMPLATE([HAVE_LIBEXIF],
            [Do we have libexif installed?])

PKG_CHECK_MODULES(LIBEXIF, libexif, have_libexif=true, have_libexif=false)

if test x$have_libexif = "xtrue"; then
AC_DEFINE(HAVE_LIBEXIF)
fi

AM_CONDITIONAL(HAVE_LIBEXIF, test x$have_libexif = xtrue)


dnl
dnl libv4lconvert
dnl

AH_TEMPLATE([HAVE_V4LCONVERT],
            [Do we have libv4lconvert installed?])

V4LCONVERT_REQUIRED="0.5.7"
have_v4lconvert=false

AC_ARG_ENABLE(v4lconvert,
              AS_HELP_STRING(--disable-v4lconvert, [Disable libv4lconvert support (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_v4lconvert=true ;;
                 no) test_v4lconvert=false ;;
               esac],
	       test_v4lconvert=true)

if test x$test_v4lconvert = "xtrue"; then
PKG_CHECK_MODULES(V4LCONVERT, libv4lconvert >= $V4LCONVERT_REQUIRED, have_v4lconvert=true, have_v4lconvert=false)
fi

AM_CONDITIONAL(HAVE_V4LVCONVERT, test x$have_v4lconvert = xtrue)
AC_SUBST(V4LCONVERT_REQUIRED)

if test x$have_v4lconvert = xtrue; then
AC_DEFINE(HAVE_V4LCONVERT)
fi

dnl
dnl Libvisual
dnl

AH_TEMPLATE([HAVE_LV],
            [Do we have libvisual installed?])
AH_TEMPLATE([LV_PLUGIN_DIR],
            [libvisual plugin directory])

LV_REQUIRED="0.4.0"
have_LV=false
AC_ARG_ENABLE(libvisual,
              AS_HELP_STRING(--disable-libvisual, [Disable Libvisual support (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_lv=true ;;
                 no) test_lv=false ;;
               esac],
	       test_lv=true)

dnl Disable libvisual for now
test_lv="false"

if test x$test_lv = "xtrue"; then
PKG_CHECK_MODULES(LV, libvisual-0.4 >= $LV_REQUIRED, have_lv=true, have_lv=false)
fi

if test x$have_lv = "xtrue"; then
AC_DEFINE(HAVE_LV)

lv_plugin_dir=`pkg-config --variable=pluginsbasedir libvisual-0.4`
lv_plugin_dir="$lv_plugin_dir/actor"

AC_DEFINE_UNQUOTED(LV_PLUGIN_DIR, ["$lv_plugin_dir"], [libvisual plugin directory])
fi


AM_CONDITIONAL(HAVE_LV, test x$have_lv = xtrue)
AC_SUBST(LV_REQUIRED)

dnl
dnl CDrom support
dnl

have_cdio="false"

CDIO_REQUIRED="1.0.0"

AC_ARG_ENABLE(libcdio,
[AS_HELP_STRING([--disable-libcdio],[Disable libcdio (default: autodetect)])],
[case "${enableval}" in
   yes) test_cdio=true ;;
   no)  test_cdio=false ;;
esac],[test_cdio=true])

if test x$test_cdio = xtrue; then
PKG_CHECK_MODULES(CDIO, libcdio >= $CDIO_REQUIRED, have_cdio="true", have_cdio="false")
fi

if test x$have_cdio = xtrue; then
PKG_CHECK_MODULES(CDIO_CDDA, libcdio_cdda >= $CDIO_REQUIRED, have_cdio_cdda="true", have_cdio="false")
fi

if test x$have_cdio = xtrue; then
PKG_CHECK_MODULES(CDIO_PARANOIA, libcdio_paranoia >= $CDIO_REQUIRED, have_cdio_paranoia="true", have_cdio="false")
fi

if test x$have_cdio = xtrue; then
OLD_CFLAGS=$CFLAGS
CFLAGS=$CDIO_CFLAGS
AC_CHECK_HEADERS([cdio/cdda.h cdio/paranoia/cdda.h cdio/paranoia.h cdio/paranoia/paranoia.h])
CFLAGS=$OLD_CFLAGS
fi


AM_CONDITIONAL(HAVE_CDIO, test x$have_cdio = xtrue)
AC_SUBST(CDIO_REQUIRED)

dnl
dnl Musicbrainz
dnl

AH_TEMPLATE([HAVE_MUSICBRAINZ],
            [Do we have libmusicbrainz installed?])

MUSICBRAINZ_REQUIRED="2.0.2"
have_musicbrainz=false
AC_ARG_ENABLE(musicbrainz,
              AS_HELP_STRING(--disable-musicbrainz, [Disable musicbrainz support (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_musicbrainz=true ;;
                 no) test_musicbrainz=false ;;
               esac],
	       test_musicbrainz=true)

dnl We check for musicbrainz only if we already detected CDROM support
if test x$have_cdio = "xfalse"; then
test_musicbrainz=false
fi

if test x$test_musicbrainz = "xtrue"; then
PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz5 >= $MUSICBRAINZ_REQUIRED, have_musicbrainz=true, have_musicbrainz=false)
fi

if test x$have_musicbrainz = "xtrue"; then
AC_DEFINE(HAVE_MUSICBRAINZ)
fi

AM_CONDITIONAL(HAVE_MUSICBRAINZ, test x$have_musicbrainz = xtrue)
AC_SUBST(MUSICBRAINZ_REQUIRED)

dnl
dnl Libcddb
dnl

AH_TEMPLATE([HAVE_LIBCDDB],
            [Do we have libcddb installed?])

dnl LIBCDDB_REQUIRED="1.2.1"
LIBCDDB_REQUIRED="1.0.2"
have_cddb=false
AC_ARG_ENABLE(cddb,
              AS_HELP_STRING(--disable-cddb, [Disable cddb support (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_cddb=true ;;
                 no) test_cddb=false ;;
               esac],
	       test_cddb=true)

dnl We check for cddb only of we already detected CDROM support
if test x$have_cdio = "xfalse"; then
test_cddb=false
fi

if test x$test_cddb = "xtrue"; then
PKG_CHECK_MODULES(CDDB, libcddb >= $LIBCDDB_REQUIRED, have_cddb=true, have_cddb=false)
fi

if test x$have_cddb = "xtrue"; then
AC_DEFINE(HAVE_LIBCDDB)
fi

AM_CONDITIONAL(HAVE_LIBCDDB, test x$have_cddb = xtrue)
AC_SUBST(LIBCDDB_REQUIRED)



dnl
dnl ALSA
dnl

ALSA_REQUIRED="1.0.0"
have_alsa="false"

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS

AC_ARG_ENABLE(alsa,
              AS_HELP_STRING(--disable-alsa, [Disable Alsa (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_alsa=true ;;
                 no) test_alsa=false ;;
               esac],
	       test_alsa=true)
		 
if test x$test_alsa = xtrue; then
   PKG_CHECK_MODULES(ALSA, alsa, have_alsa="true", have_alsa="false")
fi

dnl CFLAGS and LIBS must be cleared after AM_PATH_ALSA
CFLAGS=$OLD_CFLAGS
LIBS=$OLD_LIBS


AM_CONDITIONAL(HAVE_ALSA, test x$have_alsa = xtrue)
AC_SUBST(ALSA_REQUIRED)

dnl
dnl Video4linux2
dnl

GMERLIN_CHECK_V4L2


dnl
dnl libjpeg
dnl

GMERLIN_CHECK_LIBJPEG

dnl
dnl png
dnl

GMERLIN_CHECK_LIBPNG

dnl
dnl libtiff
dnl 

GMERLIN_CHECK_LIBTIFF

dnl
dnl inotify
dnl

GMERLIN_CHECK_INOTIFY

dnl
dnl Applications
dnl

dnl Player

AC_ARG_ENABLE(player,
              AS_HELP_STRING(--disable-player, [Disable player (default: enabled)]),
              [case "${enableval}" in
                 yes) have_player=true ;;
                 no) have_player=false ;;
               esac],
	       have_player=true)

if test "x$have_gtk" != "xtrue"; then
have_player=false
fi

AM_CONDITIONAL(HAVE_PLAYER, test x$have_player = xtrue)


dnl Transcoder

AC_ARG_ENABLE(transcoder,
              AS_HELP_STRING(--disable-transcoder, [Disable transcoder (default: enabled)]),
              [case "${enableval}" in
                 yes) have_transcoder=true ;;
                 no) have_transcoder=false ;;
               esac],
	       have_transcoder=true)

if test "x$have_gtk" = "xfalse"; then
have_transcoder=false
fi

AM_CONDITIONAL(HAVE_TRANSCODER, test x$have_transcoder = xtrue)

dnl Thumbnailer

AC_ARG_ENABLE(thumbnailer,
              AS_HELP_STRING(--disable-thumbnailer, [Disable thumbnailer (default: enabled)]),
              [case "${enableval}" in
                 yes) have_thumbnailer=true ;;
                 no) have_thumbnailer=false ;;
               esac],
	       have_thumbnailer=true)

AM_CONDITIONAL(HAVE_THUMBNAILER, test x$have_thumbnailer = xtrue)


dnl
dnl CFLAGS
dnl

LQT_OPT_CFLAGS(["-O3 -fomit-frame-pointer -ffast-math"])

CFLAGS="$CFLAGS -D_REENTRANT -D_FILE_OFFSET_BITS=64 $GAVL_CFLAGS $OPT_CFLAGS"

LQT_TRY_CFLAGS(-Wall, CFLAGS="$CFLAGS -Wall", )
LQT_TRY_CFLAGS(-Wmissing-declarations, CFLAGS="$CFLAGS -Wmissing-declarations", )
LQT_TRY_CFLAGS(-Wdeclaration-after-statement, CFLAGS="$CFLAGS -Wdeclaration-after-statement")

dnl
dnl Check for SIMD
dnl

GAVL_CHECK_SIMD($host_cpu, "$OPT_CFLAGS")

dnl
dnl libxml is necessary for all sources
dnl

CFLAGS="$CFLAGS $XML2_CFLAGS"

LIBS="$GAVL_LIBS"

dnl Output variables

if test "x$prefix" = xNONE; then
   prefix="${ac_default_prefix}"
fi
if test "x$exec_prefix" = xNONE; then
   exec_prefix="${prefix}"
fi

AC_DEFINE_UNQUOTED(PREFIX, "$prefix", Installation prefix)

AM_CPPFLAGS='-I$(top_builddir)/include'
AC_SUBST(AM_CPPFLAGS)

dnl Link all modules to libgmerlin so we can use them from within plugins
MODULE_LIBADD='$(top_builddir)/lib/libgmerlin.la'
AC_SUBST(MODULE_LIBADD)

dnl Absolute src path for doxygen

TOP_SRCDIR=`cd ${srcdir} && pwd` 
AC_SUBST(TOP_SRCDIR)

dnl LDFLAGS for plugins
GMERLIN_PLUGIN_LDFLAGS='-export-symbols $(top_srcdir)/plugin.sym' $SAVE_LDFLAGS
AC_SUBST(GMERLIN_PLUGIN_LDFLAGS)

dnl
dnl How to make symbolic links
dnl

AC_PROG_LN_S

dnl
dnl Configuration Subdirectiories
dnl

AC_CONFIG_FILES([Makefile  po/Makefile.in \
gmerlin.pc \
gmerlin-gtk.pc \
iconfont/Makefile \
web/Makefile \
web/js/Makefile \
web/icons/Makefile \
web/css/Makefile \
web/wallpapers/Makefile \
apps/Makefile \
apps/player/Makefile \
apps/cmdlineplayer/Makefile \
apps/transcoder/Makefile \
apps/thumbnailer/Makefile \
apps/mdb-tool/Makefile \
apps/mediainfo/Makefile \
apps/remote/Makefile \
apps/renderer/Makefile \
apps/server/Makefile \
doc/Makefile \
icons/Makefile \
m4/Makefile \
utils/Makefile \
tests/Makefile \
include/Makefile \
include/gmerlin/Makefile \
include/gmerlin/bg_version.h \
include/gmerlin/upnp/Makefile
include/gui_gtk/Makefile \
include/x11/Makefile \
lib/Makefile \
lib/gtk/Makefile \
lib/upnp/Makefile \
lib/x11/Makefile \
plugins/Makefile \
plugins/resources/Makefile \
plugins/backends/Makefile \
plugins/frontends/Makefile \
plugins/alsa/Makefile \
plugins/audiofilters/Makefile \
plugins/x11/Makefile \
plugins/cdaudio/Makefile \
plugins/edl/Makefile \
plugins/jpeg/Makefile \
plugins/png/Makefile \
plugins/pulseaudio/Makefile \
plugins/subwriters/Makefile \
plugins/image/Makefile \
plugins/mpris/Makefile \
plugins/tiff/Makefile \
plugins/v4l2/Makefile \
plugins/upnp/Makefile \
plugins/videofilters/Makefile \
plugins/visualization/Makefile \
plugins/visualization/glscope/Makefile \
plugins/wavwrite/Makefile ])

AC_OUTPUT

echo "=== Libraries: ===================================="

echo -n "Gtk-2.0:             "
if test "x$have_gtk" = "xtrue"; then
echo "Yes"
else
echo "Missing (GUI applications won't be compiled! Go to http://www.gtk.org/)"
fi

echo
echo "=== Plugins: ======================================"

echo -n "Alsa:                "
if test "x$have_alsa" = "xtrue"; then
echo "Yes (CFLAGS=$ALSA_CFLAGS LIBS=$ALSA_LIBS)"
else
if test "x$test_alsa" = "xtrue"; then
echo "Missing (Go to http://www.alsa-project.org/)"
else
echo "Disabled"
fi
fi

echo -n "Libvisual:           "
if test "x$have_lv" = "xtrue"; then
echo "Yes (CFLAGS=$LV_CFLAGS LIBS=$LV_LIBS)"
else
if test "x$test_lv" = "xtrue"; then
echo "Missing (Go to http://libvisual.sourceforge.net/)"
else
echo "Disabled"
fi
fi

echo -n "Libjpeg:             "
if test "x$have_libjpeg" = "xtrue"; then
echo "Yes (CFLAGS=$JPEG_CFLAGS LIBS=$JPEG_LIBS)"
else
if test "x$test_libjpeg" = "xtrue"; then
echo "Missing (Go to http://www.ijg.org/)"
else
echo "Disabled"
fi
fi

echo -n "Libpng:              "
if test "x$have_libpng" = "xtrue"; then
echo "Yes (CFLAGS=$PNG_CFLAGS LIBS=$PNG_LIBS)"
else
if test "x$test_libpng" = "xtrue"; then
echo "Missing (Go to http://www.libpng.org/pub/png/libpng.html)"
else
echo "Disabled"
fi
fi

echo -n "Libtiff:             "
if test "x$have_libtiff" = "xtrue"; then
echo "Yes (CFLAGS=$TIFF_CFLAGS LIBS=$TIFF_LIBS)"
else
if test "x$test_libtiff" = "xtrue"; then
echo "Missing (Go to http://www.remotesensing.org/libtiff/)"
else
echo "Disabled"
fi
fi

echo -n "pulseaudio:          "
if test "x$have_pulseaudio" = "xtrue"; then
echo "Yes (CFLAGS=$PULSEAUDIO_CFLAGS LIBS=$PULSEAUDIO_LIBS)"
else
if test "x$test_pulseaudio" = "xtrue"; then
echo "Missing (Go to http://www.pulseaudio.org)"
else
echo "Disabled"
fi
fi

echo -n "Video4Linux2:        "
if test "x$have_v4l2" = "xtrue"; then
echo "Yes"
else
if test "x$test_v4l2" = "xtrue"; then
echo "Missing (Probably unsupported architecture)"
else
echo "Disabled"
fi
fi

echo -n "X11:                 "
if test "x$have_x" = "xtrue"; then
echo "Yes"
else
echo "Missing"
fi

echo -n "OpenGL:              "
if test "x$have_GL" = "xtrue"; then
echo "Yes (CFLAGS=$GL_CFLAGS, LIBS=$GL_LIBS)"
else
echo "Missing"
fi

echo -n "Xinerama:            "
if test "x$have_xinerama" = "xtrue"; then
echo "Yes"
else
echo "Missing"
fi

echo -n "Xtest:               "
if test "x$have_xtest" = "xtrue"; then
echo "Yes"
else
echo "Missing"
fi



echo -n "X11 dpms extension:  "
if test "x$have_xdpms" = "xtrue"; then
echo "Yes"
else
echo "Missing"
fi

echo -n "XFixes extension:    "
if test "x$have_xfixes" = "xtrue"; then
echo "Yes"
else
echo "Missing"
fi


echo -n "libcdio:             "
if test "x$have_cdio" = "xtrue"; then
echo "Yes (CFLAGS=$CDIO_CFLAGS $CDIO_CDDA_CFLAGS $CDIO_PARANOIA_CFLAGS LIBS=$CDIO_LIBS $CDIO_CDDA_LIBS $CDIO_PARANOIA_LIBS)"
else
if test "x$test_cdio" = "xtrue"; then
echo "Missing (Go to http://www.gnu.org/software/libcdio/)"
else
echo "Disabled"
fi
fi

echo -n "Musicbrainz:         "
if test "x$have_musicbrainz" = "xtrue"; then
echo "Yes (CFLAGS=$MUSICBRAINZ_CFLAGS LIBS=$MUSICBRAINZ_LIBS)"
else
if test "x$test_musicbrainz" = "xtrue"; then
echo "Missing (Go to http://musicbrainz.org/products/client/index.html)"
else
echo "Disabled"
fi
fi

echo -n "libcddb:             "
if test "x$have_cddb" = "xtrue"; then
echo "Yes (CFLAGS=$CDDB_CFLAGS LIBS=$CDDB_LIBS)"
else
if test "x$test_cddb" = "xtrue"; then
echo "Missing (Go to http://libcddb.sourceforge.net)"
else
echo "Disabled"
fi
fi

echo -n "libv4lconvert:       "
if test "x$have_v4lconvert" = "xtrue"; then
echo "Yes (CFLAGS=$V4LCONVERT_CFLAGS LIBS=$V4LCONVERT_LIBS)"
else
if test "x$test_v4lconvert" = "xtrue"; then
echo "Missing (Go to http://people.atrpms.net/~hdegoede/)"
else
echo "Disabled"
fi
fi

echo -n "libexif:             "
if test "x$have_libexif" = "xtrue"; then
echo "Yes (CFLAGS=$LIBEXIF_CFLAGS LIBS=$LIBEXIF_LIBS)"
else
echo "Missing (Go to http://libexif.sourceforge.net/)"
fi

echo -n "ncurses:             "
if test "x$have_ncurses" = "xtrue"; then
echo "Yes (CFLAGS=$NCURSES_CFLAGS LIBS=$NCURSES_LIBS)"
else
echo "Missing"
fi


echo
echo "=== Applications: ================================="

echo -n "Player:              "
if test "x$have_player" = "xtrue"; then
echo "Enabled"
else
echo "Disabled"
fi

echo -n "Transcoder:          "
if test "x$have_transcoder" = "xtrue"; then
echo "Enabled"
else
echo "Disabled"
fi

echo -n "Thumbnailer:         "
if test "x$have_thumbnailer" = "xtrue"; then
echo "Enabled"
else
echo "Disabled"
fi


echo
echo "If you installed a library but it was not detected, check the file INSTALL"
echo "for troubleshooting tips. Also note that if the configure script reaches"
echo "this point, all missing packages are optional so compilation should succeed"
echo "anyway."
