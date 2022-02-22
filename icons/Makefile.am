
pkgdatadir = $(datadir)/gmerlin/icons

icons = \
config_16.png \
close_16.png \
fullscreen_16.png \
info_16.png \
log_16.png \
player_icon.png \
plugincfg_icon.png \
record_16.png \
recorder_icon.png \
refresh_16.png \
renderer_icon.png \
server_icon.png \
snapshot_16.png \
transcoder_icon.png \
visualizer_icon.png \
windowed_16.png 

pkgdata_DATA = $(icons)

dist_pkgdata_DATA = $(icons)

install-data-hook:
	test -z $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps || $(MKDIR_P) $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps
	ln -sf $(DESTDIR)$(pkgdatadir)/transcoder_icon.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-transcoder.png
	ln -sf $(DESTDIR)$(pkgdatadir)/plugincfg_icon.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-plugincfg.png
	ln -sf $(DESTDIR)$(pkgdatadir)/recorder_icon.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-recorder.png
	ln -sf $(DESTDIR)$(pkgdatadir)/visualizer_icon.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-visualizer.png

uninstall-local:
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-transcoder.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-plugincfg.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-recorder.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-visualizer.png


