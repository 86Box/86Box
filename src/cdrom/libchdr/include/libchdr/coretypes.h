#ifndef __CORETYPES_H__
#define __CORETYPES_H__

#include <stdint.h>
#include <stdio.h>

#ifdef USE_LIBRETRO_VFS
#include <streams/file_stream_transforms.h>
#endif

#include "macros.h"

typedef struct chd_core_file_callbacks {
	/*
	 * return the size of a given file as a 64-bit unsigned integer.
	 * the position of the file pointer after calling this function is
	 * undefined because many implementations will seek to the end of the
	 * file and call ftell.
	 *
	 * on error, (uint64_t)-1 is returned.
	 */
	uint64_t(*fsize)(void*);

	/*
	 * should match the behavior of fread, except the FILE* argument at the end
	 * will be replaced with a void*.
	 */
	size_t(*fread)(void*,size_t,size_t,void*);

	// closes the given file.
	int (*fclose)(void*);

	// fseek clone
	int (*fseek)(void*, int64_t, int);
} core_file_callbacks;

typedef struct chd_core_file_callbacks_and_argp {
	const core_file_callbacks *callbacks;

	/*
	 * arbitrary pointer to data the implementation uses to implement the above functions
	 */
	void *argp;
} core_file_callbacks_and_argp;

/* Legacy API */

typedef struct chd_core_file {
	void *argp;
	uint64_t(*fsize)(struct chd_core_file*);
	size_t(*fread)(void*,size_t,size_t,struct chd_core_file*);
	int (*fclose)(struct chd_core_file*);
	int (*fseek)(struct chd_core_file*, int64_t, int);
} core_file;

/* File IO shortcuts */

static CHDR_INLINE int core_fclose(const core_file_callbacks_and_argp *fp) {
	return fp->callbacks->fclose(fp->argp);
}

static CHDR_INLINE size_t core_fread(const core_file_callbacks_and_argp *fp, void *ptr, size_t len) {
	return fp->callbacks->fread(ptr, 1, len, fp->argp);
}

static CHDR_INLINE int core_fseek(const core_file_callbacks_and_argp* fp, int64_t offset, int whence) {
	return fp->callbacks->fseek(fp->argp, offset, whence);
}

static CHDR_INLINE uint64_t core_fsize(const core_file_callbacks_and_argp *fp)
{
	return fp->callbacks->fsize(fp->argp);
}

#endif
