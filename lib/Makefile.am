plugindir=$(pkglibdir)/plugins
pkgbindir=$(pkglibdir)/bin
AM_CPPFLAGS = -I$(top_srcdir)/include


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
dbus_sources = dbus.c 
dbus_cflags  = @DBUS_CFLAGS@
dbus_libs    = @DBUS_LIBS@
else
dbus_sources = 
dbus_cflags = 
dbus_libs = 
endif

# if HAVE_PULSEAUDIO
# pulseaudio_cflags  = @PULSEAUDIO_CFLAGS@
# pulseaudio_libs    = @PULSEAUDIO_LIBS@
# else
# pulseaudio_cflags  =
# pulseaudio_libs    =
# endif


SUBDIRS = upnp . $(gtk_subdirs)

AM_CFLAGS = \
@FONTCONFIG_CFLAGS@ \
@FREETYPE_CFLAGS@ \
@JSON_CFLAGS@ \
@CAIRO_CFLAGS@ \
@PANGO_CFLAGS@ \
@PANGOCAIRO_CFLAGS@ \
@UDEV_CFLAGS@ \
@GL_CFLAGS@ \
$(dbus_cflags) \
$(pulseaudio_cflags) \
$(ncurses_cflags) \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\"  \
-DDATA_DIR=\"$(pkgdatadir)\" \
-DPKGBIN_DIR=\"$(pkgbindir)\" \
$(lv_cflags)

libgmerlin_la_LDFLAGS = -version-info @LTVERSION_CURRENT@:@LTVERSION_REVISION@:@LTVERSION_AGE@ -export-symbols-regex ^bg_ @GMERLIN_LIB_LDFLAGS@

libgmerlin_la_LIBADD = \
upnp/libupnp.la \
@SQLITE3_LIBS@ \
@FONTCONFIG_LIBS@ \
@CAIRO_LIBS@ \
@PANGO_LIBS@ \
@PANGOCAIRO_LIBS@ \
@UDEV_LIBS@ \
$(dbus_libs) \
$(ncurses_libs) \
$(pulseaudio_libs) \
@FREETYPE_LIBS@ \
@XML2_LIBS@ \
@LIBINTL@ \
@JSON_LIBS@ \
@GL_LIBS@ \
$(lv_libs) \
$(LIBM) \
-ldl \
-lpthread

lib_LTLIBRARIES = libgmerlin.la
libgmerlin_la_SOURCES = \
$(lv_sources) \
$(dbus_sources) \
$(ncurses_sources) \
accelerator.c \
application.c \
audiofilters.c \
base64.c \
bggavl.c \
bgladspa.c \
bgfrei0r.c \
bgxml.c \
cairo.c \
cfgctx.c \
cfg_registry.c \
cfg_section.c \
cfg_xml.c \
chapterlist.c \
cleanup.c \
cmdline.c \
colormatrix.c \
control.c \
controllable.c \
country_table.c \
downloader.c \
edldec.c \
edl_xml.c \
encoder.c \
fileutils.c \
flaccover.c \
formats.c \
frametimer.c \
frontend.c \
frontend_gmerlin.c \
glvideo.c \
gl_color.c \
gl_coords.c \
gl_shader.c \
http.c \
httpconnection.c \
httpcoverhandler.c \
httplpcmhandler.c \
httpmediahandler.c \
httpplaylisthandler.c \
httpserver.c \
iconfont.c \
id3v2.c \
language_table.c \
log.c \
mdb_filesystem.c \
mdb_radiobrowser.c \
mdb_remote.c \
mdb_removable.c \
mdb_podcast.c \
mdb_recorder.c \
mdb_sqlite.c \
mdb_streams.c \
mdb_thumbnail.c \
mdb_xml.c \
mdb.c \
mediadirs.c \
mediasrcsink.c \
metadata.c \
msg.c \
msgconn.c \
msghub.c \
msgsink.c \
multiinput.c \
npt.c \
objectcache.c \
osd.c \
ov.c \
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
pluginfuncs.c \
pluginregistry.c \
pluginreg_xml.c \
radiobrowser.c \
resourcemanager.c \
ringbuffer.c \
backend.c \
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
thumbnail.c \
trackio.c \
transcoder.c \
transcoder_track.c \
translation.c \
urilist.c \
videofilters.c \
visualize.c \
websocket.c \
xspf.c

