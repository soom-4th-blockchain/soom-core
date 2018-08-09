
Debian
====================
This directory contains files used to package soomd/soom-qt
for Debian-based Linux systems. If you compile soomd/soom-qt yourself, there are some useful files here.

## soom: URI support ##


soom-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install soom-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your soom-qt binary to `/usr/bin`
and the `../../share/pixmaps/soom128.png` to `/usr/share/pixmaps`

soom-qt.protocol (KDE)

