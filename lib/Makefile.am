plugindir=$(pkglibdir)/plugins
pkgbindir=$(pkglibdir)/bin
AM_CPPFLAGS = -I$(top_srcdir)/include

if HAVE_X11
x11_subdirs = x11
x11_libadd = x11/libx11.la
else
x11_subdirs =
x11_libadd =
endif

if HAVE_GTK
gtk_subdirs = gtk
else
gtk_subdirs =
endif

if HAVE_LV
lv_sources = bglv.c
lv_cflags  = @LV_CFLAGS@
lv_libs  = @LV_LIBS@
else
lv_sources =
lv_cflags  =
lv_libs  =
endif

if HAVE_POSIX_SEMAPHORES
sem_sources =
else
sem_sources = uthread_sem.c
endif

if HAVE_NCURSES
ncurses_sources = ncurses.c
ncurses_cflags  = @NCURSES_CFLAGS@
ncurses_libs    = @NCURSES_LIBS@
else
ncurses_sources = 
ncurses_cflags  =
ncurses_libs    =
endif

if HAVE_DBUS
dbus_sources = dbus.c backend_dbus.c backend_mpris2.c frontend_player_mpris2.c backend_mpd.c
dbus_cflags  = @DBUS_CFLAGS@
dbus_libs    = @DBUS_LIBS@
else
dbus_sources = 
dbus_cflags = 
dbus_libs = 
endif

SUBDIRS = $(x11_subdirs) upnp . $(gtk_subdirs)

AM_CFLAGS = \
@FONTCONFIG_CFLAGS@ \
@FREETYPE_CFLAGS@ \
@JSON_CFLAGS@ \
@CAIRO_CFLAGS@ \
@PANGO_CFLAGS@ \
@PANGOCAIRO_CFLAGS@ \
$(dbus_cflags) \
$(ncurses_cflags) \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\"  \
-DDATA_DIR=\"$(pkgdatadir)\" \
-DPKGBIN_DIR=\"$(pkgbindir)\" \
$(lv_cflags)

libgmerlin_la_LDFLAGS = -version-info @LTVERSION_CURRENT@:@LTVERSION_REVISION@:@LTVERSION_AGE@ -export-symbols-regex ^bg_ @GMERLIN_LIB_LDFLAGS@

libgmerlin_la_LIBADD = \
$(x11_libadd) \
upnp/libupnp.la \
@SQLITE3_LIBS@ \
@FONTCONFIG_LIBS@ \
@CAIRO_LIBS@ \
@PANGO_LIBS@ \
@PANGOCAIRO_LIBS@ \
$(dbus_libs) \
$(ncurses_libs) \
@FREETYPE_LIBS@ \
@XML2_LIBS@ \
@ICONV_LIBS@ \
@MQ_LIBS@ \
@LIBINTL@ \
@JSON_LIBS@ \
@GL_LIBS@ \
$(lv_libs) \
$(LIBM) \
-ldl \
-lpthread

# pkgbin_PROGRAMS = gmerlin_visualizer_slave
# gmerlin_visualizer_slave_SOURCES = visualize_slave.c
# gmerlin_visualizer_slave_LDADD = libgmerlin.la -ldl -lpthread

lib_LTLIBRARIES = libgmerlin.la
libgmerlin_la_SOURCES = \
$(lv_sources) \
$(sem_sources) \
$(dbus_sources) \
$(ncurses_sources) \
accelerator.c \
application.c \
audiofilters.c \
base64.c \
bggavl.c \
bgladspa.c \
bgfrei0r.c \
bgplug.c \
bgplug_io.c \
bgxml.c \
cairo.c \
cfg_ctx.c \
cfg_registry.c \
cfg_section.c \
cfg_xml.c \
chapterlist.c \
charset.c \
cmdline.c \
colormatrix.c \
country_table.c \
device.c \
downloader.c \
edldec.c \
edl_xml.c \
encoder.c \
fileutils.c \
flaccover.c \
formats.c \
frametimer.c \
frontend.c \
frontend_mdb_upnp.c \
frontend_player_console.c \
frontend_player_ncurses.c \
frontend_player_upnp.c \
frontend_gmerlin.c \
hexdump.c \
http.c \
httpconnection.c \
httplpcmhandler.c \
httpmediahandler.c \
httpplaylisthandler.c \
httpserver.c \
iconfont.c \
id3v2.c \
language_table.c \
log.c \
md5.c \
mdb_filesystem.c \
mdb_radiobrowser.c \
mdb_remote.c \
mdb_podcast.c \
mdb_sqlite.c \
mdb_thumbnail.c \
mdb_xml.c \
mdb.c \
mediaconnector.c \
mediadirs.c \
mediasrcsink.c \
metadata.c \
msg.c \
msgconn.c \
msgqueue.c \
npt.c \
objectcache.c \
ocr.c \
osd.c \
ov.c \
ovl2text.c \
parameter.c \
parameter_xml.c \
player.c \
player_input.c \
player_oa.c \
player_ov.c \
player_audio.c \
player_loop.c \
player_subtitle.c \
player_thread.c \
player_time.c \
player_tracklist.c \
player_video.c \
playercmd.c \
plughandler.c \
pluginfuncs.c \
pluginregistry.c \
pluginreg_xml.c \
preset.c \
preset_xml.c \
recorder.c \
recorder_audio.c \
recorder_video.c \
recorder_input.c \
ringbuffer.c \
backend.c \
backendregistry.c \
backend_upnp.c \
backend_gmerlin.c \
searchpath.c \
serverstorage.c \
sha1.c \
sigint.c \
singlepic.c \
sqlite.c \
state.c \
streaminfo.c \
stringutils.c \
subprocess.c \
subtitle.c \
textrenderer.c \
threadpool.c \
thumbnail.c \
trackio.c \
transcoder.c \
transcoder_track.c \
translation.c \
urilist.c \
utf8.c \
videofilters.c \
visualize.c \
volumemanager.c \
websocket.c \
xspf.c

