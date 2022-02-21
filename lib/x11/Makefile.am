plugindir=$(pkglibdir)/plugins
AM_CPPFLAGS = -I$(top_srcdir)/include

if HAVE_XV
xv_sources = xvideo.c
xv_libs = @XV_LIBS@
else
xv_sources =
xv_libs =
endif

if HAVE_LIBVA
vaapi_sources = vaapi.c
vaapi_libs = @LIBVA_LIBS@
vaapi_cflags = @LIBVA_CFLAGS@
else
vaapi_sources =
vaapi_libs =
vaapi_cflags = 
endif

if HAVE_EGL


egl_sources = glvideo.c
egl_libs = @EGL_LIBS@

else

egl_sources = 
egl_libs = 

endif



if HAVE_XTEST
xtest_libs = @XTEST_LIBS@
else
xtest_libs =
endif

AM_CFLAGS = \
@X_CFLAGS@ \
-DLOCALE_DIR=\"$(localedir)\" \
-DPLUGIN_DIR=\"$(plugindir)\" \
-DDOC_DIR=\"$(docdir)\" \
-DDATA_DIR=\"$(pkgdatadir)\" \
$(vaapi_cflags)

noinst_LTLIBRARIES = \
libx11.la

libx11_la_SOURCES = \
grab.c \
icon.c \
shm.c \
x11_event.c \
x11_video.c \
x11_window.c \
overlay.c \
screensaver.c \
ximage.c \
$(egl_sources) \
$(xv_sources) \
$(vaapi_sources)

libx11_la_LIBADD =  @XINERAMA_LIBS@ @XFIXES_LIBS@ @XDPMS_LIBS@ $(xv_libs) $(glx_libs) $(egl_libs) $(xtest_libs) $(vaapi_libs) @X_LIBS@
