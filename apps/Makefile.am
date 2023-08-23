
if HAVE_PLAYER
player_subdirs = player
else
player_subdirs =
endif

if HAVE_TRANSCODER
transcoder_subdirs = transcoder
else
transcoder_subdirs =
endif

if HAVE_THUMBNAILER
thumbnailer_subdirs = thumbnailer
else
thumbnailer_subdirs =
endif

if HAVE_TOYS
toys_subdirs = toys
else
toys_subdirs =
endif

SUBDIRS = $(player_subdirs) \
cmdlineplayer \
cmdlinerecorder \
controlpanel \
mdb-tool \
mediainfo \
playerproxy \
remote \
renderer \
server \
$(transcoder_subdirs) \
$(thumbnailer_subdirs) \
$(toys_subdirs) 
