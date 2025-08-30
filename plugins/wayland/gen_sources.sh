#!/bin/sh 

# XDG Window manager stuff

wayland-scanner client-header \
    /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
    xdg-shell-client-protocol.h

wayland-scanner private-code \
    /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
    xdg-shell-client-protocol.c

# XDG decoration stuff

wayland-scanner client-header \
  /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml \
  xdg-decoration.h

wayland-scanner private-code \
  /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml \
  xdg-decoration.c

