/*
 * system_compression.h - declarations for accessing System Compressed files
 *
 * Copyright (C) 2015 Eric Biggers
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

#ifndef _NTFS_SYSTEM_COMPRESSION_H
#define _NTFS_SYSTEM_COMPRESSION_H

#include <stddef.h>
#include <sys/types.h>

#include <ntfs-3g/inode.h>
#include <ntfs-3g/types.h>

/* System compressed file access  */

struct system_decompression_ctx;

extern s64 ntfs_get_system_compressed_file_size(ntfs_inode *ni,
						const REPARSE_POINT *reparse);

extern struct ntfs_system_decompression_ctx *
ntfs_open_system_decompression_ctx(ntfs_inode *ni,
				   const REPARSE_POINT *reparse);

extern ssize_t
ntfs_read_system_compressed_data(struct ntfs_system_decompression_ctx *ctx,
				 s64 pos, size_t count, void *buf);

extern void
ntfs_close_system_decompression_ctx(struct ntfs_system_decompression_ctx *ctx);

/* XPRESS decompression  */

struct xpress_decompressor;

extern struct xpress_decompressor *xpress_allocate_decompressor(void);

extern int xpress_decompress(struct xpress_decompressor *decompressor,
		      const void *compressed_data, size_t compressed_size,
		      void *uncompressed_data, size_t uncompressed_size);

extern void xpress_free_decompressor(struct xpress_decompressor *decompressor);

/* LZX decompression  */

struct lzx_decompressor;

extern struct lzx_decompressor *lzx_allocate_decompressor(void);

extern int lzx_decompress(struct lzx_decompressor *decompressor,
			  const void *compressed_data, size_t compressed_size,
			  void *uncompressed_data, size_t uncompressed_size);

extern void lzx_free_decompressor(struct lzx_decompressor *decompressor);

#endif /* _NTFS_SYSTEM_COMPRESSION_H */
