
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

SUBDIRS = $(player_subdirs) \
cmdlineplayer \
cmdlinerecorder \
mdb-tool \
mediainfo \
remote \
renderer \
server \
$(transcoder_subdirs) \
$(thumbnailer_subdirs)
