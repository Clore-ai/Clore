
Debian
====================
This directory contains files used to package clore_blockchaind/clore-qt
for Debian-based Linux systems. If you compile clore_blockchaind/clore-qt yourself, there are some useful files here.

## clore: URI support ##


clore-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install clore-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your clore-qt binary to `/usr/bin`
and the `../../share/pixmaps/clore128.png` to `/usr/share/pixmaps`

clore-qt.protocol (KDE)

