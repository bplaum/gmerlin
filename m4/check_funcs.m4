dnl
dnl Standardized linker flags:
dnl We use --as-needed for executables and
dnl --no-undefined for libraries
dnl

AC_DEFUN([GMERLIN_CHECK_LDFLAGS],[

GMERLIN_LIB_LDFLAGS=""
GMERLIN_EXE_LDFLAGS=""

AC_MSG_CHECKING(if linker supports --no-undefined)
OLD_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--no-undefined"

AC_LINK_IFELSE([AC_LANG_SOURCE([[int main() { return 0; } ]])],
            [GMERLIN_LIB_LDFLAGS="-Wl,--no-undefined $GMERLIN_LIB_LDFLAGS"; AC_MSG_RESULT(Supported)],
            [AC_MSG_RESULT(Unsupported)])
LDFLAGS=$OLD_LDFLAGS

AC_MSG_CHECKING(if linker supports --as-needed)
OLD_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--as-needed"
AC_LINK_IFELSE([AC_LANG_SOURCE([[int main() { return 0; }]])],
            [GMERLIN_EXE_LDFLAGS="-Wl,--as-needed $GMERLIN_EXE_LDFLAGS"; AC_MSG_RESULT(Supported)],
            [AC_MSG_RESULT(Unsupported)])
LDFLAGS=$OLD_LDFLAGS

AC_SUBST(GMERLIN_LIB_LDFLAGS)
AC_SUBST(GMERLIN_EXE_LDFLAGS)

])

dnl
dnl Check for pulseaudio
dnl

AC_DEFUN([GMERLIN_CHECK_PULSEAUDIO],[

AH_TEMPLATE([HAVE_PULSEAUDIO],
            [Do we have pulseaudio installed?])

have_pulseaudio="false"

PULSEAUDIO_REQUIRED="0.9.6"

AC_ARG_ENABLE(pulseaudio,
[AS_HELP_STRING([--disable-pulseaudio],[Disable pulseaudio (default: autodetect)])],
[case "${enableval}" in
   yes) test_pulseaudio=true ;;
   no)  test_pulseaudio=false ;;
esac],[test_pulseaudio=true])

if test x$test_pulseaudio = xtrue; then

PKG_CHECK_MODULES(PULSEAUDIO, libpulse-simple >= $PULSEAUDIO_REQUIRED, have_pulseaudio="true", have_pulseaudio="false")

fi

AC_SUBST(PULSEAUDIO_REQUIRED)
AC_SUBST(PULSEAUDIO_LIBS)
AC_SUBST(PULSEAUDIO_CFLAGS)

AM_CONDITIONAL(HAVE_PULSEAUDIO, test x$have_pulseaudio = xtrue)

if test "x$have_pulseaudio" = "xtrue"; then
AC_DEFINE([HAVE_PULSEAUDIO])
fi

])

dnl
dnl Check for pipewire
dnl

AC_DEFUN([GMERLIN_CHECK_PIPEWIRE],[

AH_TEMPLATE([HAVE_PIPEWIRE],
            [Do we have pipewire installed?])

have_pipewire="false"


AC_ARG_ENABLE(pipewire,
[AS_HELP_STRING([--disable-pipewire],[Disable pipewire (default: autodetect)])],
[case "${enableval}" in
   yes) test_pipewire=true ;;
   no)  test_pipewire=false ;;
esac],[test_pipewire=true])

if test x$test_pipewire = xtrue; then
PKG_CHECK_MODULES(PIPEWIRE, libpipewire-0.3, have_pipewire="true", have_pipewire="false")
fi



AC_SUBST(PIPEWIRE_REQUIRED)
AC_SUBST(PIPEWIRE_LIBS)
AC_SUBST(PIPEWIRE_CFLAGS)

AM_CONDITIONAL(HAVE_PIPEWIRE, test x$have_pipewire = xtrue)

if test "x$have_pipewire" = "xtrue"; then
AC_DEFINE([HAVE_PIPEWIRE])
fi

])



nl
dnl libtiff
dnl

AC_DEFUN([GMERLIN_CHECK_LIBTIFF],[

AH_TEMPLATE([HAVE_LIBTIFF], [Enable tiff codec])
 
have_libtiff=false
TIFF_REQUIRED="3.5.0"

AC_ARG_ENABLE(libtiff,
[AS_HELP_STRING([--disable-libtiff],[Disable libtiff (default: autodetect)])],
[case "${enableval}" in
   yes) test_libtiff=true ;;
   no)  test_libtiff=false ;;
esac],[test_libtiff=true])

if test x$test_libtiff = xtrue; then
   
OLD_LIBS=$LIBS

LIBS="$LIBS -ltiff"
   
AC_MSG_CHECKING(for libtiff)
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <tiffio.h>
                                int main()
                                {
                                TIFF * tiff = (TIFF*)0;
                                int i = 0;
                                /* We ensure the function is here but never call it */
                                if(i)
                                  TIFFReadRGBAImage(tiff, 0, 0, (uint32*)0, 0);
                                return 0;    
                                }]])],
            [have_libtiff=true])
 
case $have_libtiff in
  true) AC_DEFINE(HAVE_LIBTIFF)
        AC_MSG_RESULT(yes)
        TIFF_LIBS=$LIBS;;
  false) AC_MSG_RESULT(no); TIFF_LIBS=""; TIFF_CFLAGS="";;
esac
LIBS=$OLD_LIBS

fi

AC_SUBST(TIFF_CFLAGS)
AC_SUBST(TIFF_LIBS)
AC_SUBST(TIFF_REQUIRED)

AM_CONDITIONAL(HAVE_LIBTIFF, test x$have_libtiff = xtrue)

if test x$have_libtiff = xtrue; then
AC_DEFINE(HAVE_LIBTIFF)
fi

])

dnl
dnl PNG
dnl 

AC_DEFUN([GMERLIN_CHECK_LIBPNG],[

AH_TEMPLATE([HAVE_LIBPNG], [Enable png codec])
 
have_libpng=false
PNG_REQUIRED="1.2.2"

AC_ARG_ENABLE(libpng,
[AS_HELP_STRING([--disable-libpng],[Disable libpng (default: autodetect)])],
[case "${enableval}" in
   yes) test_libpng=true ;;
   no)  test_libpng=false ;;
esac],[test_libpng=true])

if test x$test_libpng = xtrue; then

PKG_CHECK_MODULES(PNG, libpng, have_libpng="true", have_libpng="false")
fi

AC_SUBST(PNG_CFLAGS)
AC_SUBST(PNG_LIBS)
AC_SUBST(PNG_REQUIRED)

AM_CONDITIONAL(HAVE_LIBPNG, test x$have_libpng = xtrue)

if test x$have_libpng = xtrue; then
AC_DEFINE(HAVE_LIBPNG)
fi

])

dnl
dnl CDrom support
dnl

AC_DEFUN([GMERLIN_CHECK_CDIO],[

AH_TEMPLATE([HAVE_CDIO], [ libcdio found ])

have_cdio="false"
CDIO_REQUIRED="0.79"

AC_ARG_ENABLE(libcdio,
[AS_HELP_STRING([--disable-libcdio],[Disable libcdio (default: autodetect)])],
[case "${enableval}" in
   yes) test_cdio=true ;;
   no)  test_cdio=false ;;
esac],[test_cdio=true])

if test x$test_cdio = xtrue; then
PKG_CHECK_MODULES(CDIO, libcdio >= $CDIO_REQUIRED, have_cdio="true", have_cdio="false")
fi

AM_CONDITIONAL(HAVE_CDIO, test x$have_cdio = xtrue)
AC_SUBST(CDIO_REQUIRED)

if test "x$have_cdio" = "xtrue"; then
AC_DEFINE([HAVE_CDIO])
fi

])

dnl
dnl libjpeg
dnl

AC_DEFUN([GMERLIN_CHECK_LIBJPEG],[

AH_TEMPLATE([HAVE_LIBJPEG],
            [Do we have libjpeg installed?])

have_libjpeg=false
JPEG_REQUIRED="6b"

AC_ARG_ENABLE(libjpeg,
[AS_HELP_STRING([--disable-libjpeg],[Disable libjpeg (default: autodetect)])],
[case "${enableval}" in
   yes) test_libjpeg=true ;;
   no)  test_libjpeg=false ;;
esac],[test_libjpeg=true])

if test x$test_libjpeg = xtrue; then
PKG_CHECK_MODULES(JPEG, libjpeg, have_libjpeg="true", have_libjpeg="false")

if test "x$have_libjpeg" = "xtrue"; then
AC_DEFINE([HAVE_LIBJPEG])
fi
fi

AC_SUBST(JPEG_LIBS)
AC_SUBST(JPEG_CFLAGS)
AC_SUBST(JPEG_REQUIRED)
AM_CONDITIONAL(HAVE_LIBJPEG, test x$have_libjpeg = xtrue)

])

dnl
dnl OpenGL
dnl
AC_DEFUN([GMERLIN_CHECK_OPENGL],[
AH_TEMPLATE([HAVE_GL],[OpenGL available])
AH_TEMPLATE([HAVE_GLX],[GLX available])
AH_TEMPLATE([HAVE_EGL],[EGL available])

dnl
dnl Search for OpenGL libraries
dnl

OLD_LIBS=$LIBS

have_GL="true"
AC_SEARCH_LIBS([glBegin], [GL], [], AC_MSG_ERROR([OpenGL not found]), [])

if test "x$have_GL" = "xtrue"; then
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <GL/gl.h>
				 int main()
				 {
				 if(0)
				   glBegin(GL_QUADS); return 0;
				 } ]])],
	       [],AC_MSG_ERROR([Linking OpenGL program failed]))
fi

GL_LIBS=$LIBS

LIBS="$OLD_LIBS"

dnl
dnl Check for EGL
dnl

OLD_LIBS=$LIBS

have_EGL="true"
AC_SEARCH_LIBS([eglGetCurrentDisplay], [GL EGL], [], AC_MSG_ERROR([EGL not found]), [])

if test "x$have_GL" = "xtrue"; then
AC_LINK_IFELSE([AC_LANG_SOURCE([[#include <EGL/egl.h>
				 int main()
				 {
				 if(0)
				   eglGetCurrentDisplay();
				 return 0;
				 }
				 ]])],
               [],AC_MSG_ERROR([Linking EGL program failed]))
fi

EGL_LIBS=$LIBS

LIBS="$OLD_LIBS"

if test "x$have_GL" = "xtrue"; then
AC_DEFINE(HAVE_GL)

if test "x$have_EGL" = "xtrue"; then
AC_DEFINE(HAVE_EGL)
fi

fi

AM_CONDITIONAL(HAVE_GL, test x$have_GL = xtrue)
AM_CONDITIONAL(HAVE_EGL, test x$have_EGL = xtrue)

AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_LIBS)
AC_SUBST(EGL_CFLAGS)
AC_SUBST(EGL_LIBS)

])

dnl
dnl GLU
dnl

AC_DEFUN([GMERLIN_CHECK_GLU],[
AH_TEMPLATE([HAVE_GLU],[GLU available])

OLD_CFLAGS=$CFLAGS
OLD_LIBS=$LIBS

have_GLU="true"
AC_SEARCH_LIBS([gluLookAt], [GLU], [], [have_GLU="false"], [])

if test "x$have_GLU" = "xtrue"; then
AC_TRY_LINK([#include <GL/glu.h>],[
if(0) gluLookAt(0, 0, 0, 0, 0, 0, 0, 0, 0); return 0;
],[],[have_GLU="false"])
fi

GLU_CFLAGS=$CFLAGS
GLU_LIBS=$LIBS

CFLAGS="$OLD_CFLAGS"
LIBS="$OLD_LIBS"

if test "x$have_GLU" = "xtrue"; then
AC_DEFINE(HAVE_GLU)
fi

AM_CONDITIONAL(HAVE_GLU, test x$have_GLU = xtrue)

AC_SUBST(GLU_CFLAGS)
AC_SUBST(GLU_LIBS)

])

dnl
dnl inotify
dnl

AC_DEFUN([GMERLIN_CHECK_INOTIFY],[
have_inotify="false"
AH_TEMPLATE([HAVE_INOTIFY], [System supports inotify])
AC_CHECK_FUNC(inotify_init,have_inotify="true";AC_DEFINE(HAVE_INOTIFY))

])

dnl
dnl Semaphores
dnl

AC_DEFUN([GMERLIN_CHECK_SEMAPHORES],[
AH_TEMPLATE([HAVE_POSIX_SEMAPHORES], [System supports POSIX semaphores])

have_posix_semaphores="false"

OLD_LIBS=$LIBS
LIBS="$LIBS -lpthread"

AC_MSG_CHECKING([for POSIX unnamed semaphores]);

  AC_TRY_RUN([
    #include <semaphore.h>
	  
    #include <stdio.h>
    int main()
    {
    int result;
    sem_t s;
    result = sem_init(&s, 0, 0);
    if(result)
      return -1;
    return 0;
    }
  ],
  [
    # program could be run
    have_posix_semaphores="true"
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_POSIX_SEMAPHORES)
  ],[
    # program could not be run
    AC_MSG_RESULT(no)
  ],[
    # cross compiling
    have_posix_semaphores="true"
    AC_MSG_RESULT([assuming yes (cross compiling)])
    AC_DEFINE(HAVE_POSIX_SEMAPHORES)
  ]
)

LIBS=$OLD_LIBS


AM_CONDITIONAL(HAVE_POSIX_SEMAPHORES, test x$have_posix_semaphores = xtrue)

])

dnl
dnl Video4linux2
dnl

AC_DEFUN([GMERLIN_CHECK_V4L2],[

AH_TEMPLATE([HAVE_V4L2], [Enable v4l2])
	     
have_v4l2=false
AC_ARG_ENABLE(v4l2,
              AS_HELP_STRING(--disable-v4l2, [Disable Video4Linux (default: autodetect)]),
              [case "${enableval}" in
                 yes) test_v4l2=true ;;
                 no) test_v4l2=false ;;
               esac],
	       test_v4l2=true)

if test x$test_v4l2 = xtrue; then
AC_CHECK_HEADERS(linux/videodev2.h, have_v4l2=true)
fi

AM_CONDITIONAL(HAVE_V4L2, test x$have_v4l2 = xtrue)

if test x$have_v4l2 = xtrue; then
AC_DEFINE(HAVE_V4L2)
fi


])

dnl
dnl X11
dnl

AC_DEFUN([GMERLIN_CHECK_X11],[

have_x="false"

X_CFLAGS=""
X_LIBS=""

AH_TEMPLATE([HAVE_XLIB],
            [Do we have xlib installed?])

AC_PATH_X

if test x$no_x != xyes; then
  if test "x$x_includes" != "x"; then
    X_CFLAGS="-I$x_includes"
  elif test -d /usr/X11R6/include; then 
    X_CFLAGS="-I/usr/X11R6/include"
  else
    X_CFLAGS=""
  fi

  if test "x$x_libraries" != "x"; then
    X_LIBS="-L$x_libraries -lX11"
  else
    X_LIBS="-lX11"
  fi
  have_x="true"
else
  PKG_CHECK_MODULES(X, x11 >= 1.0.0, have_x=true, have_x=false)
fi

if test x$have_x = xtrue; then
  X_LIBS="$X_LIBS -lXext"
  AC_DEFINE([HAVE_XLIB])
fi


AC_SUBST(X_CFLAGS)
AC_SUBST(X_LIBS)
AM_CONDITIONAL(HAVE_X11, test x$have_x = xtrue)

])


dnl
dnl libva
dnl

AC_DEFUN([GMERLIN_CHECK_LIBVA],[

AH_TEMPLATE([HAVE_LIBVA],
            [Do we have libva installed?])
AH_TEMPLATE([HAVE_LIBVA_X11],
            [Do we have libva (x11) installed?])

have_libva="false"
have_libva_glx="false"
have_libva_x11="false"

LIBVA_CFLAGS=""
LIBVA_LIBS=""

AC_ARG_ENABLE(libva,
[AS_HELP_STRING([--disable-libva],[Disable libva (default: autodetect)])],
[case "${enableval}" in
   yes) test_libva=true ;;
   no)  test_libva=false ;;
esac],[test_libva=true])

if test x$have_x != xtrue; then
test_libva="false"
fi

if test x$test_libva = xtrue; then
PKG_CHECK_MODULES(LIBVA_BASE, libva, have_libva="true", have_libva="false")
fi

if test "x$have_libva" = "xtrue"; then
LIBVA_CFLAGS=$LIBVA_BASE_CFLAGS
LIBVA_LIBS=$LIBVA_BASE_LIBS

if test x$have_x = xtrue; then
PKG_CHECK_MODULES(LIBVA_X11, libva-x11, have_libva_x11="true", have_libva_x11="false")
fi

if test "x$have_libva_x11" = "xtrue"; then
LIBVA_CFLAGS="$LIBVA_CFLAGS $LIBVA_X11_CFLAGS"
LIBVA_LIBS="$LIBVA_LIBS $LIBVA_X11_LIBS"
else
have_libva="false"
fi

fi

AC_SUBST(LIBVA_LIBS)
AC_SUBST(LIBVA_CFLAGS)

AM_CONDITIONAL(HAVE_LIBVA, test x$have_libva = xtrue)
AM_CONDITIONAL(HAVE_LIBVA_X11, test x$have_libva_x11 = xtrue)

if test "x$have_libva" = "xtrue"; then
AC_DEFINE([HAVE_LIBVA])
fi

if test "x$have_libva_x11" = "xtrue"; then
AC_DEFINE([HAVE_LIBVA_X11])
fi

])
