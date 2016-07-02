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

#include <fuse.h>

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <ntfs-3g/inode.h>
#include <ntfs-3g/plugin.h>

#include "system_compression.h"

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

static int compressed_open(ntfs_inode *ni __attribute__((unused)),
			   const REPARSE_POINT *reparse __attribute__((unused)),
			   struct fuse_file_info *fi)
{
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EOPNOTSUPP;
	return 0;
}

static int compressed_release(ntfs_inode *ni __attribute__((unused)),
			   const REPARSE_POINT *reparse __attribute__((unused)),
			   struct fuse_file_info *fi __attribute__((unused)))
{
	return 0;
}

static int compressed_read(ntfs_inode *ni, const REPARSE_POINT *reparse,
			   char *buf, size_t size, off_t offset,
			   struct fuse_file_info *fi __attribute__((unused)))
{
	struct ntfs_system_decompression_ctx *dctx;
	ssize_t res;

	/* TODO: there needs to be more investigation into reusing decompression
	 * contexts for multiple reads. */

	dctx = ntfs_open_system_decompression_ctx(ni, reparse);
	if (!dctx)
		return -errno;

	res = ntfs_read_system_compressed_data(dctx, offset, size, buf);

	ntfs_close_system_decompression_ctx(dctx);

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
