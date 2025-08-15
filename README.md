# Overview

System compression, also known as "Compact OS", is a Windows feature that allows
rarely modified files to be compressed using the XPRESS or LZX compression
formats.  It is not built directly into NTFS but rather is implemented using
reparse points.  This feature appeared in Windows 10 and it appears that many
Windows 10 systems have been using it by default.

This repository contains a plugin which enables the NTFS-3G FUSE driver to
transparently read from system-compressed files.  It must be built against
NTFS-3G version 2017.3.23 or later, since that was the first stable version to
include support for reparse point plugins.

Currently, only reading is supported.  Compressing an existing file may be done
by using the "compact" utility on Windows, with one of the options below
("xpress4k" is the weakest and fastest, "lzx" is the strongest and slowest):

	/exe:xpress4k
	/exe:xpress8k
	/exe:xpress16k
	/exe:lzx

# Installation

First, either clone the git repository, or download and extract the latest
release tarball from
https://github.com/ebiggers/ntfs-3g-system-compression/releases.

Then, install prerequisite packages.  For example, on Ubuntu and other
Debian-based systems, run:

    sudo apt install make gcc autoconf automake libtool pkg-config ntfs-3g ntfs-3g-dev

Then, run the following to build and install the plugin:

    # Set LIBDIR to the parent directory of the NTFS-3G plugin directory.
    # This is usually /usr/lib or /usr/lib/x86_64-linux-gnu.
    # The below command tries to find it automatically.
    LIBDIR=$(strings $(which ntfs-3g) | grep ntfs-plugin | sed 's@/ntfs-3g/.*@@')

    autoreconf -i
    ./configure --libdir="$LIBDIR"
    make
    sudo make install

# Implementation note

The XPRESS and LZX compression formats used in system-compressed files are
identical to the formats used in Windows Imaging (WIM) archives.  Therefore, for
the system compression plugin I borrowed the XPRESS and LZX decompressors I had
already written for the wimlib project (https://wimlib.net/).  I made some
slight modifications for integration purposes, and I relicensed the files that
used the LGPLv3+ license to GPLv2+ for compatibility with NTFS-3G's license.

# Notices

The NTFS-3G system compression plugin was written by Eric Biggers, with
contributions from Jean-Pierre André.  You can contact the author at
ebiggers3@gmail.com.

This software may be redistributed and/or modified under the terms of the GNU
General Public License as published by the Free Software Foundation, either
version 2 of the License, or (at your option) any later version.  There is NO
WARRANY, to the extent permitted by law.  See the COPYING file for details.
