EXTRA_DIST=Doxyfile.in \
mainpage.incl \
img \
gmerlin_remote.texi \
gmerlin_transcoder.texi \
gmerlin_transcoder_remote.texi \
gmerlin_play.texi \
gmerlin_player.texi \
gmerlin_record.texi \
plugins.texi \
gavl.tags

if HAVE_DOXYGEN
doxygen_output = apiref
else
doxygen_output = 
endif

info_TEXINFOS = gmerlin.texi
texinfo_output = userguide

gmerlin_TEXINFOS= $(info_TEXINFOS) version.texi

userguide: $(gmerlin_TEXINFOS)
	ln -sf `pwd`"/$(top_srcdir)/doc/img" $(top_builddir)/img
	$(MAKEINFO) -I $(top_srcdir)/doc $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) \
                --html -o userguide $(top_srcdir)/doc/gmerlin.texi
	rm -f $(top_builddir)/img

apiref:
	doxygen Doxyfile

all: html

CLEANFILES = gmerlin.tags

html: $(doxygen_output) $(texinfo_output)

install-data-local:
	if test "x$(doxygen_output)" != "x"; then \
          test -z "$(DESTDIR)$(docdir)/userguide" || $(MKDIR_P) "$(DESTDIR)$(docdir)/userguide" ; \
          $(INSTALL_DATA) userguide/* $(DESTDIR)$(docdir)/userguide ; \
        fi
	if test "x$(texinfo_output)" != "x"; then \
          test -z "$(DESTDIR)$(docdir)/apiref" || $(MKDIR_P) "$(DESTDIR)$(docdir)/apiref" ; \
          $(INSTALL_DATA) apiref/* $(DESTDIR)$(docdir)/apiref ; \
          test -z "$(DESTDIR)$(docdir)/img" || $(MKDIR_P) "$(DESTDIR)$(docdir)/img" ; \
          $(INSTALL_DATA) $(top_srcdir)/doc/img/*.png $(DESTDIR)$(docdir)/img ; \
        fi

clean-local:
	-rm -rf apiref
	-rm -rf userguide

uninstall-local:
	-rm -rf "$(DESTDIR)$(docdir)/apiref"
	-rm -rf "$(DESTDIR)$(docdir)/userguide"
	-rm -rf "$(DESTDIR)$(docdir)/img"

