
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

if HAVE_RECORDER
recorder_subdirs = recorder
else
recorder_subdirs =
endif

if HAVE_VISUALIZER
visualizer_subdirs = visualizer
else
visualizer_subdirs =
endif

if HAVE_THUMBNAILER
thumbnailer_subdirs = thumbnailer
else
thumbnailer_subdirs =
endif

SUBDIRS = $(player_subdirs) \
cmdlineplayer \
cmdlinerecorder \
controlpanel \
gavftools \
mdb-tool \
mediainfo \
playerproxy \
remote \
renderer \
server \
$(recorder_subdirs) \
$(transcoder_subdirs) \
$(thumbnailer_subdirs)
