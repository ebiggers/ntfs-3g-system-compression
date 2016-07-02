/*
 * system_compression.c - Support for reading System Compressed files
 *
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

/*
 * Windows 10 introduced a new filesystem compression feature: System
 * Compression, also called "Compact OS".  The feature allows rarely modified
 * files to be compressed more heavily than is possible with regular NTFS
 * compression (which uses the LZNT1 algorithm with 4096-byte chunks).
 * System-compressed files can only be read, not written; on Windows, if a
 * program attempts to write to such a file, it is automatically decompressed
 * and turned into an ordinary uncompressed file.
 *
 * Rather than building it directly into NTFS, Microsoft implemented this new
 * compression mode using the Windows Overlay Filesystem (WOF) filter driver
 * that was added in Windows 8.1.  A system-compressed file contains the
 * following NTFS attributes:
 *
 * - A reparse point attribute in the format WOF_FILE_PROVIDER_REPARSE_POINT_V1,
 *   documented below
 * - A sparse unnamed data attribute, containing all zero bytes, with data size
 *   equal to the uncompressed file size
 * - A data attribute named "WofCompressedData" containing the compressed data
 *   of the file.
 *
 * The compressed data contains a series of chunks, each of which decompresses
 * to a known size determined by the compression format specified in the reparse
 * point.  The last chunk can be an exception, since it decompresses to whatever
 * size remains.  Chunks that did not compress to less than their original size
 * are stored uncompressed.  The compressed chunks are concatenated in order and
 * are prefixed by a table of 4-byte (for files < 4 GiB in size uncompressed) or
 * 8-byte (for files >= 4 GiB in size uncompressed) little endian numbers which
 * give the offset of each compressed chunk from the end of the table.  Since
 * every chunk can be decompressed independently and its location can be
 * discovered from the chunk offset table, "random access" reads are possible
 * with chunk granularity.  Writes are not possible, in general, without
 * rewriting the entire file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <ntfs-3g/attrib.h>
#include <ntfs-3g/layout.h>
#include <ntfs-3g/misc.h>

#include "system_compression.h"

/******************************************************************************/

/* Known values of the WOF protocol / reparse point format  */
typedef enum {
	WOF_CURRENT_VERSION	= const_cpu_to_le32(1),
} WOF_VERSION;

/* Known WOF providers  */
typedef enum {
	/* WIM backing provider ("WIMBoot")  */
	WOF_PROVIDER_WIM	= const_cpu_to_le32(1),

	/* System compressed file provider  */
	WOF_PROVIDER_FILE	= const_cpu_to_le32(2),
} WOF_PROVIDER;

/* Known versions of the compressed file provider  */
typedef enum {
	WOF_FILE_PROVIDER_CURRENT_VERSION	= const_cpu_to_le32(1),
} WOF_FILE_PROVIDER_VERSION;

/* Information needed to specify a WOF provider  */
typedef struct {
	le32 version;
	le32 provider;
} WOF_EXTERNAL_INFO;

/* Metadata for the compressed file provider --- indicates how the file
 * is compressed  */
typedef struct {
	le32 version;
	le32 compression_format;
} WOF_FILE_PROVIDER_EXTERNAL_INFO_V1;

/* Format of the reparse point attribute of system compressed files  */
typedef struct {
	/* The reparse point header.  This indicates that the reparse point is
	 * supposed to be interpreted by the WOF filter driver.  */
	REPARSE_POINT reparse;

	/* The WOF provider specification.  This indicates the "provider" that
	 * the WOF filter driver is supposed to hand control to.  */
	WOF_EXTERNAL_INFO wof;

	/* The metadata specific to the compressed file "provider"  */
	WOF_FILE_PROVIDER_EXTERNAL_INFO_V1 file;

} WOF_FILE_PROVIDER_REPARSE_POINT_V1;

/* The available compression formats for system compressed files  */
typedef enum {
	FORMAT_XPRESS4K		= const_cpu_to_le32(0),
	FORMAT_LZX		= const_cpu_to_le32(1),
	FORMAT_XPRESS8K		= const_cpu_to_le32(2),
	FORMAT_XPRESS16K	= const_cpu_to_le32(3),
} WOF_FILE_PROVIDER_COMPRESSION_FORMAT;

/* "WofCompressedData": the name of the named data stream which contains the
 * compressed data of a system compressed file  */
static ntfschar compressed_stream_name[] = {
	const_cpu_to_le16('W'), const_cpu_to_le16('o'),
	const_cpu_to_le16('f'), const_cpu_to_le16('C'),
	const_cpu_to_le16('o'), const_cpu_to_le16('m'),
	const_cpu_to_le16('p'), const_cpu_to_le16('r'),
	const_cpu_to_le16('e'), const_cpu_to_le16('s'),
	const_cpu_to_le16('s'), const_cpu_to_le16('e'),
	const_cpu_to_le16('d'), const_cpu_to_le16('D'),
	const_cpu_to_le16('a'), const_cpu_to_le16('t'),
	const_cpu_to_le16('a'),
};

/******************************************************************************/

/* The maximum number of chunk offsets that may be cached at any one time.  This
 * is purely an implementation detail, and this number can be changed.  The
 * minimum possible value is 2, and the maximum possible value is UINT32_MAX
 * divided by the maximum chunk size.  */
#define NUM_CHUNK_OFFSETS	128

/* A special marker value not used by any chunk index  */
#define INVALID_CHUNK_INDEX	UINT64_MAX

/* The decompression context for a system compressed file  */
struct ntfs_system_decompression_ctx {

	/* The open compressed stream ("WofCompressedData")  */
	ntfs_attr *na;

	/* The compression format of the file  */
	WOF_FILE_PROVIDER_COMPRESSION_FORMAT format;

	/* The decompressor for the file  */
	void *decompressor;

	/* The uncompressed size of the file in bytes  */
	u64 uncompressed_size;

	/* The compressed size of the file in bytes  */
	u64 compressed_size;

	/* The number of chunks into which the file is divided  */
	u64 num_chunks;

	/* The base 2 logarithm of chunk_size  */
	u32 chunk_order;

	/* The uncompressed chunk size in bytes.  All chunks have this
	 * uncompressed size except possibly the last.  */
	u32 chunk_size;

	/*
	 * The chunk offsets cache.  If 'base_chunk_idx == INVALID_CHUNK_INDEX',
	 * then the cache is empty.  Otherwise, 'base_chunk_idx' is the 0-based
	 * index of the chunk that has its offset cached in 'chunk_offsets[0]'.
	 * The offsets of the subsequent chunks follow until either the array is
	 * full or the offset of the file's last chunk has been cached.  There
	 * is an extra entry at end-of-file which contains the end-of-file
	 * offset.  All offsets are stored relative to 'base_chunk_offset'.
	 */
	u64 base_chunk_idx;
	u64 base_chunk_offset;
	u32 chunk_offsets[NUM_CHUNK_OFFSETS];

	/* A temporary buffer used to hold the compressed chunk currently being
	 * decompressed or the chunk offset data currently being parsed.  */
	void *temp_buffer;

	/*
	 * A cache for the most recently decompressed chunk.  'cached_chunk' is
	 * a buffer which, if 'cached_chunk_idx != INVALID_CHUNK_INDEX',
	 * contains the uncompressed data of the chunk with index
	 * 'cached_chunk_idx'.
	 *
	 * This cache is intended to prevent adjacent reads with lengths shorter
	 * than the chunk size from causing redundant chunk decompressions.
	 * It's not intended to be a general purpose data cache.
	 */
	void *cached_chunk;
	u64 cached_chunk_idx;
};

static int allocate_decompressor(struct ntfs_system_decompression_ctx *ctx)
{
	if (ctx->format == FORMAT_LZX)
		ctx->decompressor = lzx_allocate_decompressor();
	else
		ctx->decompressor = xpress_allocate_decompressor();
	if (!ctx->decompressor)
		return -1;
	return 0;
}

static void free_decompressor(struct ntfs_system_decompression_ctx *ctx)
{
	if (ctx->format == FORMAT_LZX)
		lzx_free_decompressor(ctx->decompressor);
	else
		xpress_free_decompressor(ctx->decompressor);
}

static int decompress(struct ntfs_system_decompression_ctx *ctx,
		      const void *compressed_data, size_t compressed_size,
		      void *uncompressed_data, size_t uncompressed_size)
{
	if (ctx->format == FORMAT_LZX)
		return lzx_decompress(ctx->decompressor,
				      compressed_data, compressed_size,
				      uncompressed_data, uncompressed_size);
	else
		return xpress_decompress(ctx->decompressor,
					 compressed_data, compressed_size,
					 uncompressed_data, uncompressed_size);
}

static int get_compression_format(ntfs_inode *ni, const REPARSE_POINT *reparse,
				  WOF_FILE_PROVIDER_COMPRESSION_FORMAT *format_ret)
{
	WOF_FILE_PROVIDER_REPARSE_POINT_V1 *rp;
	s64 rpbuflen;
	int ret;

	if (!ni) {
		errno = EINVAL;
		return -1;
	}

	/* Is this a reparse point file?  */
	if (!(ni->flags & FILE_ATTR_REPARSE_POINT)) {
		errno = EOPNOTSUPP;
		return -1;
	}

	/* Read the reparse point if not done already.  */
	if (reparse) {
		rp = (WOF_FILE_PROVIDER_REPARSE_POINT_V1 *)reparse;
		rpbuflen = sizeof(REPARSE_POINT) +
			   le16_to_cpu(reparse->reparse_data_length);
	} else {
		rp = ntfs_attr_readall(ni, AT_REPARSE_POINT, AT_UNNAMED, 0,
				       &rpbuflen);
		if (!rp)
			return -1;
	}

	/* Does the reparse point indicate a system compressed file?  */
	if (rpbuflen >= (s64)sizeof(WOF_FILE_PROVIDER_REPARSE_POINT_V1) &&
	    rp->reparse.reparse_tag == IO_REPARSE_TAG_WOF &&
	    rp->wof.version == WOF_CURRENT_VERSION &&
	    rp->wof.provider == WOF_PROVIDER_FILE &&
	    rp->file.version == WOF_FILE_PROVIDER_CURRENT_VERSION &&
	    (rp->file.compression_format == FORMAT_XPRESS4K ||
	     rp->file.compression_format == FORMAT_XPRESS8K ||
	     rp->file.compression_format == FORMAT_XPRESS16K ||
	     rp->file.compression_format == FORMAT_LZX))
	{
		/* Yes, it's a system compressed file.  Save the compression
		 * format identifier.  */
		*format_ret = rp->file.compression_format;
		ret = 0;
	} else {
		/* No, it's not a system compressed file.  */
		errno = EOPNOTSUPP;
		ret = -1;
	}

	if ((const REPARSE_POINT *)rp != reparse)
		free(rp);
	return ret;
}

static u32 get_chunk_order(WOF_FILE_PROVIDER_COMPRESSION_FORMAT format)
{
	switch (format) {
	case FORMAT_XPRESS4K:
		return 12;
	case FORMAT_XPRESS8K:
		return 13;
	case FORMAT_XPRESS16K:
		return 14;
	case FORMAT_LZX:
		return 15;
	}
	/* Not reached  */
	return 0;
}

/*
 * ntfs_get_system_compressed_file_size - Return the compressed size of a system
 * compressed file
 *
 * @ni:		The NTFS inode for the file
 * @reparse:	(Optional) the contents of the file's reparse point attribute
 *
 * On success, return the compressed size in bytes.  On failure, return -1 and
 * set errno.  If the file is not a system compressed file, return -1 and set
 * errno to EOPNOTSUPP.
 */
s64 ntfs_get_system_compressed_file_size(ntfs_inode *ni,
					 const REPARSE_POINT *reparse)
{
	WOF_FILE_PROVIDER_COMPRESSION_FORMAT format;
	ntfs_attr_search_ctx *actx;
	s64 ret;

	/* Verify this is a system compressed file.  */
	if (get_compression_format(ni, reparse, &format))
		return -1;

	/* Get the size of the WofCompressedData named data stream.  */

	actx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!actx)
		return -1;

	ret = ntfs_attr_lookup(AT_DATA, compressed_stream_name,
			       sizeof(compressed_stream_name) /
					sizeof(compressed_stream_name[0]),
			       CASE_SENSITIVE, 0, NULL, 0, actx);
	if (!ret)
		ret = ntfs_get_attribute_value_length(actx->attr);

	ntfs_attr_put_search_ctx(actx);

	return ret;
}

/*
 * ntfs_open_system_decompression_ctx - Open a system-compressed file
 *
 * @ni:		The NTFS inode for the file
 * @reparse:	(Optional) the contents of the file's reparse point attribute
 *
 * On success, return a pointer to the decompression context.  On failure,
 * return NULL and set errno.  If the file is not a system-compressed file,
 * return NULL and set errno to EOPNOTSUPP.
 */
struct ntfs_system_decompression_ctx *
ntfs_open_system_decompression_ctx(ntfs_inode *ni, const REPARSE_POINT *reparse)
{
	WOF_FILE_PROVIDER_COMPRESSION_FORMAT format;
	struct ntfs_system_decompression_ctx *ctx;

	/* Get the compression format.  This also validates that the file really
	 * is a system-compressed file.  */
	if (get_compression_format(ni, reparse, &format))
		goto err;

	/* Allocate the decompression context.  */
	ctx = ntfs_malloc(sizeof(struct ntfs_system_decompression_ctx));
	if (!ctx)
		goto err;

	/* Allocate the decompressor.  */
	ctx->format = format;
	if (allocate_decompressor(ctx))
		goto err_free_ctx;

	/* Open the WofCompressedData stream.  */
	ctx->na = ntfs_attr_open(ni, AT_DATA, compressed_stream_name,
				 sizeof(compressed_stream_name) /
					sizeof(compressed_stream_name[0]));
	if (!ctx->na)
		goto err_free_decompressor;

	/* The uncompressed size of a system-compressed file is the size of its
	 * unnamed data stream, which should be sparse so that it consumes no
	 * disk space (though we don't rely on it being sparse).  */
	ctx->uncompressed_size = ni->data_size;

	/* Get the chunk size, which depends on the compression format.  */
	ctx->chunk_order = get_chunk_order(ctx->format);
	ctx->chunk_size = (u32)1 << ctx->chunk_order;

	/* Compute the number of chunks into which the file is divided.  */
	ctx->num_chunks = (ctx->uncompressed_size +
			   ctx->chunk_size - 1) >> ctx->chunk_order;

	/* The compressed size of a system compressed file is the size of its
	 * WofCompressedData stream.  */
	ctx->compressed_size = ctx->na->data_size;

	/* Initially, no chunk offsets are cached.  */
	ctx->base_chunk_idx = INVALID_CHUNK_INDEX;

	/* Allocate buffers for chunk data.  */
	ctx->temp_buffer = ntfs_malloc(max(ctx->chunk_size,
					   NUM_CHUNK_OFFSETS * sizeof(u64)));
	ctx->cached_chunk = ntfs_malloc(ctx->chunk_size);
	ctx->cached_chunk_idx = INVALID_CHUNK_INDEX;
	if (!ctx->temp_buffer || !ctx->cached_chunk)
		goto err_close_ctx;

	return ctx;

err_close_ctx:
	free(ctx->cached_chunk);
	free(ctx->temp_buffer);
	ntfs_attr_close(ctx->na);
err_free_decompressor:
	free_decompressor(ctx);
err_free_ctx:
	free(ctx);
err:
	return NULL;
}

/* Retrieve the stored offset and size of a chunk stored in the compressed file
 * stream.  */
static int get_chunk_location(struct ntfs_system_decompression_ctx *ctx,
			      u64 chunk_idx,
			      u64 *offset_ret, u32 *stored_size_ret)
{
	size_t cache_idx;

	/* To get the stored size of the chunk, we need its offset and the next
	 * chunk's offset.  Use the cached values if possible; otherwise load
	 * the needed offsets into the cache.  To reduce the number of chunk
	 * table reads that may be required later, also load some extra.  */
	if (chunk_idx < ctx->base_chunk_idx ||
	    chunk_idx + 1 >= ctx->base_chunk_idx + NUM_CHUNK_OFFSETS)
	{
		const u64 start_chunk = chunk_idx;
		const u64 end_chunk =
			chunk_idx + min(NUM_CHUNK_OFFSETS - 1,
					ctx->num_chunks - chunk_idx);
		const int entry_shift =
			(ctx->uncompressed_size <= UINT32_MAX) ? 2 : 3;
		le32 * const offsets32 = ctx->temp_buffer;
		le64 * const offsets64 = ctx->temp_buffer;
		u64 first_entry_to_read;
		size_t num_entries_to_read;
		size_t i, j;
		s64 res;

		num_entries_to_read = end_chunk - start_chunk;

		/* The first chunk has no explicit chunk table entry.  */
		if (start_chunk == 0) {
			num_entries_to_read--;
			first_entry_to_read = 0;
		} else {
			first_entry_to_read = start_chunk - 1;
		}

		if (end_chunk != ctx->num_chunks)
			num_entries_to_read++;

		/* Read the chunk table entries into a temporary buffer.  */
		res = ntfs_attr_pread(ctx->na,
				      first_entry_to_read << entry_shift,
				      num_entries_to_read << entry_shift,
				      ctx->temp_buffer);

		if ((u64)res != num_entries_to_read << entry_shift) {
			if (res >= 0)
				errno = EINVAL;
			ctx->base_chunk_idx = INVALID_CHUNK_INDEX;
			return -1;
		}

		/* Prepare the cached chunk offsets.  */

		i = 0;
		if (start_chunk == 0) {
			/* Implicit first entry  */
			ctx->chunk_offsets[i++] = 0;
			ctx->base_chunk_offset = 0;
		} else {
			if (entry_shift == 3) {
				ctx->base_chunk_offset =
					le64_to_cpu(offsets64[0]);
			} else {
				ctx->base_chunk_offset =
					le32_to_cpu(offsets32[0]);
			}
		}

		if (entry_shift == 3) {
			/* 64-bit entries (huge file)  */
			for (j = 0; j < num_entries_to_read; j++) {
				ctx->chunk_offsets[i++] =
					le64_to_cpu(offsets64[j]) -
					ctx->base_chunk_offset;
			}
		} else {
			/* 32-bit entries  */
			for (j = 0; j < num_entries_to_read; j++) {
				ctx->chunk_offsets[i++] =
					le32_to_cpu(offsets32[j]) -
					ctx->base_chunk_offset;
			}
		}

		/* Account for the chunk table itself.  */
		ctx->base_chunk_offset += (ctx->num_chunks - 1) << entry_shift;

		if (end_chunk == ctx->num_chunks) {
			/* Implicit last entry  */
			ctx->chunk_offsets[i] = ctx->compressed_size -
						ctx->base_chunk_offset;
		}

		ctx->base_chunk_idx = start_chunk;
	}

	cache_idx = chunk_idx - ctx->base_chunk_idx;
	*offset_ret = ctx->base_chunk_offset + ctx->chunk_offsets[cache_idx];
	*stored_size_ret = ctx->chunk_offsets[cache_idx + 1] -
			   ctx->chunk_offsets[cache_idx];
	return 0;
}

/* Retrieve into @buffer the uncompressed data of chunk @chunk_idx.  */
static int read_and_decompress_chunk(struct ntfs_system_decompression_ctx *ctx,
				     u64 chunk_idx, void *buffer)
{
	u64 offset;
	u32 stored_size;
	u32 uncompressed_size;
	void *read_buffer;
	s64 res;

	/* Get the location of the chunk data as stored in the file.  */
	if (get_chunk_location(ctx, chunk_idx, &offset, &stored_size))
		return -1;

	/* All chunks decompress to 'chunk_size' bytes except possibly the last,
	 * which decompresses to whatever remains.  */
	if (chunk_idx == ctx->num_chunks - 1)
		uncompressed_size = ((ctx->uncompressed_size - 1) &
				     (ctx->chunk_size - 1)) + 1;
	else
		uncompressed_size = ctx->chunk_size;

	/* Forbid strange compressed sizes.  */
	if (stored_size <= 0 || stored_size > uncompressed_size) {
		errno = EINVAL;
		return -1;
	}

	/* Chunks that didn't compress to less than their original size are
	 * stored uncompressed.  */
	if (stored_size == uncompressed_size) {
		/* Chunk is stored uncompressed  */
		read_buffer = buffer;
	} else {
		/* Chunk is stored compressed  */
		read_buffer = ctx->temp_buffer;
	}

	/* Read the stored chunk data.  */
	res = ntfs_attr_pread(ctx->na, offset, stored_size, read_buffer);
	if (res != stored_size) {
		if (res >= 0)
			errno = EINVAL;
		return -1;
	}

	/* If the chunk was stored uncompressed, then we're done.  */
	if (read_buffer == buffer)
		return 0;

	/* The chunk was stored compressed.  Decompress its data.  */
	return decompress(ctx, read_buffer, stored_size,
			  buffer, uncompressed_size);
}

/* Retrieve a pointer to the uncompressed data of the specified chunk.  On
 * failure, return NULL and set errno.  */
static const void *get_chunk_data(struct ntfs_system_decompression_ctx *ctx,
				  u64 chunk_idx)
{
	if (chunk_idx != ctx->cached_chunk_idx) {
		ctx->cached_chunk_idx = INVALID_CHUNK_INDEX;
		if (read_and_decompress_chunk(ctx, chunk_idx, ctx->cached_chunk))
			return NULL;
		ctx->cached_chunk_idx = chunk_idx;
	}
	return ctx->cached_chunk;
}

/*
 * ntfs_read_system_compressed_data - Read data from a system-compressed file
 *
 * @ctx:	The decompression context for the file
 * @pos:	The byte offset into the uncompressed data to read from
 * @count:	The number of bytes of uncompressed data to read
 * @buf:	The buffer into which to read the data
 *
 * On full or partial success, return the number of bytes read (0 indicates
 * end-of-file).  On complete failure, return -1 and set errno.
 */
ssize_t ntfs_read_system_compressed_data(struct ntfs_system_decompression_ctx *ctx,
					 s64 pos, size_t count, void *buf)
{
	u64 offset;
	u8 *p;
	u8 *end_p;
	u64 chunk_idx;
	u32 offset_in_chunk;
	u32 chunk_size;

	if (!ctx || pos < 0) {
		errno = EINVAL;
		return -1;
	}

	offset = (u64)pos;
	if (offset >= ctx->uncompressed_size)
		return 0;

	count = min(count, ctx->uncompressed_size - offset);
	if (!count)
		return 0;

	p = buf;
	end_p = p + count;
	chunk_idx = offset >> ctx->chunk_order;
	offset_in_chunk = offset & (ctx->chunk_size - 1);
	chunk_size = ctx->chunk_size;
	do {
		u32 len_to_copy;
		const u8 *chunk;

		if (chunk_idx == ctx->num_chunks - 1)
			chunk_size = ((ctx->uncompressed_size - 1) &
				      (ctx->chunk_size - 1)) + 1;

		len_to_copy = min((size_t)(end_p - p),
				  chunk_size - offset_in_chunk);

		chunk = get_chunk_data(ctx, chunk_idx);
		if (!chunk)
			break;

		memcpy(p, &chunk[offset_in_chunk], len_to_copy);

		p += len_to_copy;
		chunk_idx++;
		offset_in_chunk = 0;
	} while (p != end_p);

	return (p == buf) ? -1 : p - (u8 *)buf;
}

/*
 * ntfs_close_system_decompression_ctx - Close a system-compressed file
 */
void ntfs_close_system_decompression_ctx(struct ntfs_system_decompression_ctx *ctx)
{
	if (ctx) {
		free(ctx->cached_chunk);
		free(ctx->temp_buffer);
		ntfs_attr_close(ctx->na);
		free_decompressor(ctx);
		free(ctx);
	}
}
