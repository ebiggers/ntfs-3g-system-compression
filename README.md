# Overview

System compression, also known as "Compact OS", is a Windows feature that allows
rarely modified files to be compressed using the XPRESS or LZX compression
formats.  It is not built directly into NTFS but rather is implemented using
reparse points.  This feature appeared in Windows 10 and it appears that many
Windows 10 systems have been using it by default.

This repository contains a plugin which enables the NTFS-3G FUSE driver to
transparently read from system-compressed files.  It must be built against
NTFS-3G version 2016.2.22AR.1 or later, since that was the first version to
include support for reparse point plugins.

Currently, only reading is supported.  Compressing an existing file may be done
by using the "compact" utility on Windows, with one of the options below
("xpress4k" is the weakest and fastest, "lzx" is the strongest and slowest):

	/exe:xpress4k
	/exe:xpress8k
	/exe:xpress16k
	/exe:lzx

# Installation

The plugin can be built by running `./configure && make`.  The build system must
be able to find the NTFS-3G library and headers.  On some platforms this may
require that the "ntfs-3g-dev" package or similar be installed in addition to
the main "ntfs-3g" package.

After compiling, run `make install` to install the plugin to the NTFS-3G plugin
directory, which will be a subdirectory "ntfs-3g" of the system library
directory (`$libdir`).  An example full path to the installed plugin is
`/usr/lib/ntfs-3g/ntfs-plugin-80000017.so`.  It may differ slightly on different
platforms.  `make install` will create the plugin directory if it does not
already exist.

# License

This software may be redistributed and/or modified under the terms of the GNU
General Public License as published by the Free Software Foundation, either
version 2 of the License, or (at your option) any later version.  There is NO
WARRANY, to the extent permitted by law.  See the COPYING file for details.
