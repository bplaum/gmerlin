plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = \
@GTK_CFLAGS@ \
@XML2_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\"

AM_LDFLAGS = @GMERLIN_EXE_LDFLAGS@

if HAVE_GTK
gtk_programs = \
cfgtest \
mdb-tree \
question \
message \
chapter \
vumeter
else
gtk_programs =
endif

noinst_PROGRAMS = audioplayer1 \
videoplayer1 \
fvtest \
msgtest \
server \
client \
dump_plugins \
extractchannel \
insertchannel \
ocrtest \
remotedevreg \
textrenderer \
ladspa \
makethumbnail \
msgiotest \
ssdp \
sqlextract \
upnpdesc \
websocket_test \
$(gtk_programs) \
gmerlin_imgconvert \
gmerlin-upnp-discover \
gmerlin_imgdiff \
gmerlin_imgsplit \
gmerlin_psnr \
gmerlin_ssim \
gmerlin_vanalyze \
gmerlin_vpsnr

# noinst_HEADERS = player1.h

audioplayer1_SOURCES = audioplayer1.c
audioplayer1_LDADD = ../lib/libgmerlin.la -ldl

sqlextract_SOURCES = sqlextract.c
sqlextract_LDADD = ../lib/libgmerlin.la -ldl  @SQLITE3_LIBS@

ocrtest_SOURCES = ocrtest.c
ocrtest_LDADD = ../lib/libgmerlin.la -ldl

remotedevreg_SOURCES = remotedevreg.c
remotedevreg_LDADD = ../lib/libgmerlin.la -ldl

msgiotest_SOURCES = msgiotest.c
msgiotest_LDADD = ../lib/libgmerlin.la -ldl

ssdp_SOURCES = ssdp.c
ssdp_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_upnp_discover_LDADD = ../lib/libgmerlin.la -ldl

upnpdesc_SOURCES = upnpdesc.c
upnpdesc_LDADD = ../lib/libgmerlin.la -ldl @UUID_LIBS@ @XML2_LIBS@

extractchannel_SOURCES = extractchannel.c
extractchannel_LDADD = ../lib/libgmerlin.la -ldl

websocket_test_LDADD = ../lib/libgmerlin.la -ldl


insertchannel_SOURCES = insertchannel.c
insertchannel_LDADD = ../lib/libgmerlin.la -ldl


dump_plugins_SOURCES = dump_plugins.c
dump_plugins_LDADD = ../lib/libgmerlin.la -ldl

ladspa_SOURCES = ladspa.c
ladspa_LDADD =  -ldl

makethumbnail_SOURCES = makethumbnail.c
makethumbnail_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_imgconvert_SOURCES = imgconvert.c
gmerlin_imgconvert_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_imgsplit_SOURCES = gmerlin_imgsplit.c
gmerlin_imgsplit_LDADD = ../lib/libgmerlin.la -ldl


gmerlin_psnr_SOURCES = gmerlin_psnr.c
gmerlin_psnr_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_ssim_SOURCES = gmerlin_ssim.c
gmerlin_ssim_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_vpsnr_SOURCES = gmerlin_vpsnr.c
gmerlin_vpsnr_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_vanalyze_SOURCES = gmerlin_vanalyze.c
gmerlin_vanalyze_LDADD = ../lib/libgmerlin.la -ldl

# gmerlin_visualize_SOURCES = gmerlin_visualize.c
# gmerlin_visualize_LDADD = ../lib/libgmerlin.la -ldl

gmerlin_imgdiff_SOURCES = gmerlin_imgdiff.c
gmerlin_imgdiff_LDADD = ../lib/libgmerlin.la -ldl

msgtest_SOURCES = msgtest.c
msgtest_LDADD = ../lib/libgmerlin.la -ldl -lpthread

textrenderer_SOURCES = textrenderer.c
textrenderer_LDADD = ../lib/libgmerlin.la -ldl

videoplayer1_SOURCES = videoplayer1.c
videoplayer1_LDADD = ../lib/libgmerlin.la -ldl

fvtest_SOURCES = fvtest.c
fvtest_LDADD = ../lib/libgmerlin.la -ldl


cfgtest_SOURCES = cfgtest.c
cfgtest_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la

mdb_tree_SOURCES = mdb-tree.c
mdb_tree_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la @GTK_LIBS@


chapter_SOURCES = chapter.c
chapter_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la

vumeter_SOURCES = vumeter.c
vumeter_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la @GTK_LIBS@

# visualization_SOURCES = visualization.c
# visualization_LDADD = ../lib/libgmerlin.la

question_SOURCES = question.c
question_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la

message_SOURCES = message.c
message_LDADD = ../lib/libgmerlin.la ../lib/gtk/libgmerlin_gtk.la

server_SOURCES = server.c
server_LDADD = ../lib/libgmerlin.la

client_SOURCES = client.c
client_LDADD = ../lib/libgmerlin.la

