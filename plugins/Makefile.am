if HAVE_X11
x11_subdirs = x11
else
x11_subdirs =
endif


if HAVE_LIBPNG
png_subdirs = png
else
png_subdirs =
endif

if HAVE_LIBTIFF
tiff_subdirs = tiff
else
tiff_subdirs =
endif

if HAVE_V4L2
v4l2_subdirs = v4l2
else
v4l2_subdirs =
endif

if HAVE_LIBJPEG
jpeg_subdirs = jpeg
else
jpeg_subdirs =
endif

if HAVE_ALSA
alsa_subdirs = alsa
else
alsa_subdirs = 
endif

if HAVE_PULSEAUDIO
pulseaudio_subdirs = pulseaudio
else
pulseaudio_subdirs = 
endif

if HAVE_PIPEWIRE
# pipewire_subdirs = pipewire
pipewire_subdirs =
else
pipewire_subdirs = 
endif

if HAVE_DBUS
dbus_subdirs = mpris
else
dbus_subdirs = 
endif


if HAVE_CDIO
cdio_subdirs = cdaudio
else
cdio_subdirs = 
endif

SUBDIRS = \
audiofilters \
backends \
frontends \
edl \
resources \
videofilters \
visualization \
$(alsa_subdirs) \
$(dbus_subdirs) \
$(jpeg_subdirs) \
$(v4l2_subdirs) \
$(png_subdirs) \
$(tiff_subdirs) \
$(x11_subdirs) \
$(cdio_subdirs) \
$(pipewire_subdirs) \
$(pulseaudio_subdirs) \
upnp \
wavwrite \
image \
subwriters


