EXTRA_DIST=Doxyfile.in \
mainpage.incl \
img \
gavl.tags

if HAVE_DOXYGEN
doxygen_output = apiref
else
doxygen_output = 
endif

apiref:
	doxygen Doxyfile

all: html

CLEANFILES = gmerlin.tags

html: $(doxygen_output) 

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

uninstall-local:
	-rm -rf "$(DESTDIR)$(docdir)/apiref"
	-rm -rf "$(DESTDIR)$(docdir)/img"

