/*
 * plugin.c - NTFS-3G system compression plugin
 *
 * Copyright (C) 2015 Jean-Pierre Andre
 * Copyright (C) 2015-2016 Eric Biggers
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <ntfs-3g/inode.h>
#include <ntfs-3g/plugin.h>

#include "system_compression.h"

/* ntfs-3g/plugin.h is missing this definition, so define it here... */
struct fuse_file_info {
	int flags;
	unsigned long fh_old;
	int writepage;
	unsigned int direct_io : 1;
	unsigned int keep_cache : 1;
	unsigned int flush : 1;
	unsigned int padding : 29;
	uint64_t fh;
	uint64_t lock_owner;
};

/*
 * For each open file description for a system-compressed file, we cache an
 * ntfs_system_decompression_ctx for the file in the FUSE file handle.
 *
 * A decompression context includes a decompressor, cached data, and cached
 * metadata.  It does not include an open ntfs_inode for the file or an open
 * ntfs_attr for the file's compressed stream.  This is necessary because
 * NTFS-3G is not guaranteed to keep the inode open the whole time the file is
 * open.  Indeed, NTFS-3G may close an inode after a read request and re-open it
 * for the next one, though it does maintain an open inode cache.
 *
 * As a result of the decompression context caching, the results of reads from a
 * system-compressed file that has been written to since being opened for
 * reading are unspecified.  Stale data might be returned.  Currently, this
 * doesn't matter because this plugin blocks writes to system-compressed files.
 * (It might still be possible for adventurous users to play with the
 * WofCompressedData named data stream directly.)
 */
#define DECOMPRESSION_CTX(fi) \
	((struct ntfs_system_decompression_ctx *)(uintptr_t)((fi)->fh))

static int compressed_getattr(ntfs_inode *ni, const REPARSE_POINT *reparse,
			      struct stat *stbuf)
{
	s64 compressed_size = ntfs_get_system_compressed_file_size(ni, reparse);

	if (compressed_size >= 0) {
		/* System-compressed file */
		stbuf->st_size = ni->data_size;
		stbuf->st_blocks = (compressed_size + 511) >> 9;
		stbuf->st_mode = S_IFREG | 0555;
		return 0;
	}

	/* Not a system compressed file, or another error occurred */
	return -errno;
}

static int compressed_open(ntfs_inode *ni, const REPARSE_POINT *reparse,
			   struct fuse_file_info *fi)
{
	struct ntfs_system_decompression_ctx *dctx;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EOPNOTSUPP;

	dctx = ntfs_open_system_decompression_ctx(ni, reparse);
	if (!dctx)
		return -errno;

	fi->fh = (uintptr_t)dctx;
	return 0;
}

static int compressed_release(ntfs_inode *ni __attribute__((unused)),
			   const REPARSE_POINT *reparse __attribute__((unused)),
			   struct fuse_file_info *fi)
{
	ntfs_close_system_decompression_ctx(DECOMPRESSION_CTX(fi));
	return 0;
}

static int compressed_read(ntfs_inode *ni, const REPARSE_POINT *reparse,
			   char *buf, size_t size, off_t offset,
			   struct fuse_file_info *fi)
{
	ssize_t res;

	res = ntfs_read_system_compressed_data(DECOMPRESSION_CTX(fi), ni,
					       offset, size, buf);
	if (res < 0)
		return -errno;
	return res;
}

static const struct plugin_operations ops = {
	.getattr = compressed_getattr,
	.open = compressed_open,
	.release = compressed_release,
	.read = compressed_read,
};

const struct plugin_operations *init(le32 tag)
{
	if (tag == IO_REPARSE_TAG_WOF)
		return &ops;
	errno = EINVAL;
	return NULL;
}
