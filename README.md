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

First, if you're building from the git repository instead of from a release
tarball, you'll need to generate the `configure` script by running
`autoreconf -i`.  This requires GNU autoconf, automake, and libtool.

The plugin can then be built by running `./configure && make`.  The build system
must be able to find the NTFS-3G library and headers.  On some platforms this
may require that the "ntfs-3g-dev" package or similar be installed in addition
to the main "ntfs-3g" package.

After compiling, run `make install` to install the plugin to the NTFS-3G plugin
directory, which will be a subdirectory "ntfs-3g" of the system library
directory (`$libdir`).  An example full path to the installed plugin is
`/usr/lib/ntfs-3g/ntfs-plugin-80000017.so`.  It may differ slightly on different
platforms.  `make install` will create the plugin directory if it does not
already exist.

# Implementation note

The XPRESS and LZX compression formats used in system-compressed files are
identical to the formats used in Windows Imaging (WIM) archives.  Therefore, for
the system compression plugin I borrowed the XPRESS and LZX decompressors I had
already written for the wimlib project (https://wimlib.net/).  I made some
slight modifications for integration purposes.  The code in wimlib is currently
licensed LGPLv3+, but I have relicensed the version in this plugin to GPLv2+ for
consistency with NTFS-3G's license.  (Public domain portions remain public
domain.)

# Known issues

Various problems occur if the NTFS volume is mounted with `lowntfs-3g` instead
of with `ntfs-3g`.  This is caused by a bug in `lowntfs-3g`, and a fix to
`lowntfs-3g` has been proposed.

# Notices

The NTFS-3G system compression plugin was written by Eric Biggers, with
contributions from Jean-Pierre André.  You can contact the author at
ebiggers3@gmail.com.

This software may be redistributed and/or modified under the terms of the GNU
General Public License as published by the Free Software Foundation, either
version 2 of the License, or (at your option) any later version.  There is NO
WARRANY, to the extent permitted by law.  See the COPYING file for details.
