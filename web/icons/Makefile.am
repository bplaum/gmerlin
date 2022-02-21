pkgdatadir = $(datadir)/gmerlin/web/icons

files = \
player_16.png \
player_48.bmp \
player_48.jpg \
player_48.png \
player_96.bmp \
player_96.jpg \
player_96.png \
remote_16.png \
remote_48.bmp \
remote_48.jpg \
remote_48.png \
remote_96.bmp \
remote_96.jpg \
remote_96.png \
renderer_16.png \
renderer_48.bmp \
renderer_48.jpg \
renderer_48.png \
renderer_96.bmp \
renderer_96.jpg \
renderer_96.png \
server_16.png \
server_48.png \
server_96.png \
server_48.bmp \
server_48.jpg \
server_96.bmp \
server_96.jpg \
movie_nocover.png \
music_nocover.png \
radio_nocover.png \
tv_nocover.png

pkgdata_DATA = $(files)
dist_pkgdata_DATA = $(files)

install-data-hook:
	test -z $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps || $(MKDIR_P) $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps
	ln -sf $(DESTDIR)$(pkgdatadir)/player_48.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-player.png
	ln -sf $(DESTDIR)$(pkgdatadir)/server_48.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-server.png
	ln -sf $(DESTDIR)$(pkgdatadir)/renderer_48.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-renderer.png
	ln -sf $(DESTDIR)$(pkgdatadir)/remote_48.png $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-remote.png

uninstall-local:
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-player.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-server.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-renderer.png
	-rm -rf $(DESTDIR)$(prefix)/share/icons/hicolor/48x48/apps/gmerlin-remote.png
