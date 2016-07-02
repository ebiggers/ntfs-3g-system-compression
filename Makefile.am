ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = README.md COPYING

plugindir = $(libdir)/ntfs-3g

plugin_LTLIBRARIES = ntfs-plugin-80000017.la

ntfs_plugin_80000017_la_SOURCES =	\
	src/decompress_common.c		\
	src/decompress_common.h		\
	src/lzx_decompress.c		\
	src/plugin.c			\
	src/system_compression.c	\
	src/system_compression.h	\
	src/xpress_decompress.c

ntfs_plugin_80000017_la_LDFLAGS  = -module -shared -avoid-version
ntfs_plugin_80000017_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64
ntfs_plugin_80000017_la_CFLAGS   = $(LIBNTFS_3G_CFLAGS)
ntfs_plugin_80000017_la_LIBADD   = $(LIBNTFS_3G_LIBS)
