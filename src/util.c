/*
 * util.c
 */

/*
 * Copyright (C) 2012, 2013 Eric Biggers
 *
 * This file is part of wimlib, a library for working with WIM files.
 *
 * wimlib is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * wimlib is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wimlib; if not, see http://www.gnu.org/licenses/.
 */

#include "config.h"

#undef _GNU_SOURCE
/* Make sure the POSIX-compatible strerror_r() is declared, rather than the GNU
 * version, which has a different return type. */
#define _POSIX_C_SOURCE 200112
#include <string.h>
#define _GNU_SOURCE

#include "wimlib_internal.h"
#include "endianness.h"
#include "timestamp.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>

#include <unistd.h> /* for getpid() */

#ifdef __WIN32__
#include "win32.h"
#endif

static size_t
utf16le_strlen(const utf16lechar *s)
{
	const utf16lechar *p = s;
	while (*p)
		p++;
	return (p - s) * sizeof(utf16lechar);
}

#ifdef __WIN32__
#  define wimlib_vfprintf vfwprintf
#else
/* Handle %W for UTF16-LE printing.
 *
 * TODO: this is not yet done properly--- it's assumed that if the format string
 * contains %W, then it contains no other format specifiers.
 */
static int
wimlib_vfprintf(FILE *fp, const tchar *format, va_list va)
{
	const tchar *p;
	int n;

	for (p = format; *p; p++)
		if (*p == T('%') && *(p + 1) == T('W'))
			goto special;
	return tvfprintf(fp, format, va);
special:
	n = 0;
	for (p = format; *p; p++) {
		if (*p == T('%') && (*(p + 1) == T('W'))) {
			int ret;
			tchar *tstr;
			size_t tstr_nbytes;
			utf16lechar *ucs = va_arg(va, utf16lechar*);

			if (ucs) {
				size_t ucs_nbytes = utf16le_strlen(ucs);

				ret = utf16le_to_tstr(ucs, ucs_nbytes,
						      &tstr, &tstr_nbytes);
				if (ret) {
					ret = tfprintf(fp, T("??????"));
				} else {
					ret = tfprintf(fp, T("%"TS), tstr);
					FREE(tstr);
				}
				if (ret < 0)
					return -1;
				else
					n += ret;
			} else {
				n += tfprintf(fp, T("(null)"));
			}
			p++;
		} else {
			if (tputc(*p, fp) == EOF)
				return -1;
			n++;
		}
	}
	return n;
}

int
wimlib_printf(const tchar *format, ...)
{
	int ret;
	va_list va;

	va_start(va, format);
	ret = wimlib_vfprintf(stdout, format, va);
	va_end(va);
	return ret;
}

int
wimlib_fprintf(FILE *fp, const tchar *format, ...)
{
	int ret;
	va_list va;

	va_start(va, format);
	ret = wimlib_vfprintf(fp, format, va);
	va_end(va);
	return ret;
}
#endif

#if defined(ENABLE_ERROR_MESSAGES) || defined(ENABLE_DEBUG)
static void
wimlib_vmsg(const tchar *tag, const tchar *format,
	    va_list va, bool perror)
{
#ifndef DEBUG
	if (wimlib_print_errors) {
#endif
		int errno_save = errno;
		fflush(stdout);
		tfputs(tag, stderr);
		wimlib_vfprintf(stderr, format, va);
		if (perror && errno_save != 0) {
			tchar buf[50];
			int res;
			res = tstrerror_r(errno_save, buf, sizeof(buf));
			if (res) {
				tsprintf(buf,
					 T("unknown error (errno=%d)"),
					 errno_save);
			}
			tfprintf(stderr, T(": %"TS), buf);
		}
		tputc(T('\n'), stderr);
		errno = errno_save;
#ifndef DEBUG
	}
#endif
}
#endif

/* True if wimlib is to print an informational message when an error occurs.
 * This can be turned off by calling wimlib_set_print_errors(false). */
#ifdef ENABLE_ERROR_MESSAGES
static bool wimlib_print_errors = false;


void
wimlib_error(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[ERROR] "), format, va, false);
	va_end(va);
}

void
wimlib_error_with_errno(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[ERROR] "), format, va, true);
	va_end(va);
}

void
wimlib_warning(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[WARNING] "), format, va, false);
	va_end(va);
}

void
wimlib_warning_with_errno(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[WARNING] "), format, va, true);
	va_end(va);
}

#endif

#if defined(ENABLE_DEBUG) || defined(ENABLE_MORE_DEBUG)
void wimlib_debug(const tchar *file, int line, const char *func,
		  const tchar *format, ...)
{

	va_list va;
	tchar buf[tstrlen(file) + strlen(func) + 30];

	tsprintf(buf, T("[%"TS" %d] %s(): "), file, line, func);

	va_start(va, format);
	wimlib_vmsg(buf, format, va, false);
	va_end(va);
}
#endif

WIMLIBAPI int
wimlib_set_print_errors(bool show_error_messages)
{
#ifdef ENABLE_ERROR_MESSAGES
	wimlib_print_errors = show_error_messages;
	return 0;
#else
	if (show_error_messages)
		return WIMLIB_ERR_UNSUPPORTED;
	else
		return 0;
#endif
}

static const tchar *error_strings[] = {
	[WIMLIB_ERR_SUCCESS]
		= T("Success"),
	[WIMLIB_ERR_ALREADY_LOCKED]
		= T("The WIM is already locked for writing"),
	[WIMLIB_ERR_COMPRESSED_LOOKUP_TABLE]
		= T("Lookup table is compressed"),
	[WIMLIB_ERR_DECOMPRESSION]
		= T("Failed to decompress compressed data"),
	[WIMLIB_ERR_DELETE_STAGING_DIR]
		= T("Failed to delete staging directory"),
	[WIMLIB_ERR_FILESYSTEM_DAEMON_CRASHED]
		= T("The process servicing the mounted WIM has crashed"),
	[WIMLIB_ERR_FORK]
		= T("Failed to fork another process"),
	[WIMLIB_ERR_FUSE]
		= T("An error was returned by fuse_main()"),
	[WIMLIB_ERR_FUSERMOUNT]
		= T("Could not execute the `fusermount' program, or it exited "
			"with a failure status"),
	[WIMLIB_ERR_ICONV_NOT_AVAILABLE]
		= T("The iconv() function does not seem to work. "
		  "Maybe check to make sure the directory /usr/lib/gconv exists"),
	[WIMLIB_ERR_IMAGE_COUNT]
		= T("Inconsistent image count among the metadata "
			"resources, the WIM header, and/or the XML data"),
	[WIMLIB_ERR_IMAGE_NAME_COLLISION]
		= T("Tried to add an image with a name that is already in use"),
	[WIMLIB_ERR_INTEGRITY]
		= T("The WIM failed an integrity check"),
	[WIMLIB_ERR_INVALID_CAPTURE_CONFIG]
		= T("The capture configuration string was invalid"),
	[WIMLIB_ERR_INVALID_CHUNK_SIZE]
		= T("The WIM is compressed but does not have a chunk "
			"size of 32768"),
	[WIMLIB_ERR_INVALID_COMPRESSION_TYPE]
		= T("The WIM is compressed, but is not marked as having LZX or "
			"XPRESS compression"),
	[WIMLIB_ERR_INVALID_DENTRY]
		= T("A directory entry in the WIM was invalid"),
	[WIMLIB_ERR_INVALID_HEADER_SIZE]
		= T("The WIM header was not 208 bytes"),
	[WIMLIB_ERR_INVALID_IMAGE]
		= T("Tried to select an image that does not exist in the WIM"),
	[WIMLIB_ERR_INVALID_INTEGRITY_TABLE]
		= T("The WIM's integrity table is invalid"),
	[WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY]
		= T("An entry in the WIM's lookup table is invalid"),
	[WIMLIB_ERR_INVALID_MULTIBYTE_STRING]
		= T("A string was not valid in the current locale's character encoding"),
	[WIMLIB_ERR_INVALID_OVERLAY]
		= T("Conflicting files in overlay when creating a WIM image"),
	[WIMLIB_ERR_INVALID_PARAM]
		= T("An invalid parameter was given"),
	[WIMLIB_ERR_INVALID_PART_NUMBER]
		= T("The part number or total parts of the WIM is invalid"),
	[WIMLIB_ERR_INVALID_RESOURCE_HASH]
		= T("The SHA1 message digest of a WIM resource did not match the expected value"),
	[WIMLIB_ERR_INVALID_RESOURCE_SIZE]
		= T("A resource entry in the WIM has an invalid size"),
	[WIMLIB_ERR_INVALID_SECURITY_DATA]
		= T("The table of security descriptors in the WIM is invalid"),
	[WIMLIB_ERR_INVALID_UNMOUNT_MESSAGE]
		= T("The version of wimlib that has mounted a WIM image is incompatible with the "
		  "version being used to unmount it"),
	[WIMLIB_ERR_INVALID_UTF8_STRING]
		= T("A string provided as input by the user was not a valid UTF-8 string"),
	[WIMLIB_ERR_INVALID_UTF16_STRING]
		= T("A string in a WIM dentry is not a valid UTF-16LE string"),
	[WIMLIB_ERR_LIBXML_UTF16_HANDLER_NOT_AVAILABLE]
		= T("libxml2 was unable to find a character encoding conversion handler "
		  "for UTF-16LE"),
	[WIMLIB_ERR_LINK]
		= T("Failed to create a hard or symbolic link when extracting "
			"a file from the WIM"),
	[WIMLIB_ERR_MKDIR]
		= T("Failed to create a directory"),
	[WIMLIB_ERR_MQUEUE]
		= T("Failed to create or use a POSIX message queue"),
	[WIMLIB_ERR_NOMEM]
		= T("Ran out of memory"),
	[WIMLIB_ERR_NOTDIR]
		= T("Expected a directory"),
	[WIMLIB_ERR_NOT_A_WIM_FILE]
		= T("The file did not begin with the magic characters that "
			"identify a WIM file"),
	[WIMLIB_ERR_NO_FILENAME]
		= T("The WIM is not identified with a filename"),
	[WIMLIB_ERR_NTFS_3G]
		= T("NTFS-3g encountered an error (check errno)"),
	[WIMLIB_ERR_OPEN]
		= T("Failed to open a file"),
	[WIMLIB_ERR_OPENDIR]
		= T("Failed to open a directory"),
	[WIMLIB_ERR_READ]
		= T("Could not read data from a file"),
	[WIMLIB_ERR_READLINK]
		= T("Could not read the target of a symbolic link"),
	[WIMLIB_ERR_RENAME]
		= T("Could not rename a file"),
	[WIMLIB_ERR_REOPEN]
		= T("Could not re-open the WIM after overwriting it"),
	[WIMLIB_ERR_RESOURCE_ORDER]
		= T("The components of the WIM were arranged in an unexpected order"),
	[WIMLIB_ERR_SPECIAL_FILE]
		= T("Encountered a special file that cannot be archived"),
	[WIMLIB_ERR_SPLIT_INVALID]
		= T("The WIM is part of an invalid split WIM"),
	[WIMLIB_ERR_SPLIT_UNSUPPORTED]
		= T("The WIM is part of a split WIM, which is not supported for this operation"),
	[WIMLIB_ERR_STAT]
		= T("Could not read the metadata for a file or directory"),
	[WIMLIB_ERR_TIMEOUT]
		= T("Timed out while waiting for a message to arrive from another process"),
	[WIMLIB_ERR_UNICODE_STRING_NOT_REPRESENTABLE]
		= T("A Unicode string could not be represented in the current locale's encoding"),
	[WIMLIB_ERR_UNKNOWN_VERSION]
		= T("The WIM file is marked with an unknown version number"),
	[WIMLIB_ERR_UNSUPPORTED]
		= T("The requested operation is unsupported"),
	[WIMLIB_ERR_VOLUME_LACKS_FEATURES]
		= T("The volume did not support a feature necessary to complete the operation"),
	[WIMLIB_ERR_WRITE]
		= T("Failed to write data to a file"),
	[WIMLIB_ERR_XML]
		= T("The XML data of the WIM is invalid"),
};

WIMLIBAPI const tchar *
wimlib_get_error_string(enum wimlib_error_code code)
{
	if (code < 0 || code >= ARRAY_LEN(error_strings))
		return NULL;
	else
		return error_strings[code];
}



#ifdef ENABLE_CUSTOM_MEMORY_ALLOCATOR
void *(*wimlib_malloc_func) (size_t)	     = malloc;
void  (*wimlib_free_func)   (void *)	     = free;
void *(*wimlib_realloc_func)(void *, size_t) = realloc;

void *
wimlib_calloc(size_t nmemb, size_t size)
{
	size_t total_size = nmemb * size;
	void *p = MALLOC(total_size);
	if (p)
		memset(p, 0, total_size);
	return p;
}

char *
wimlib_strdup(const char *str)
{
	size_t size;
	char *p;

	size = strlen(str);
	p = MALLOC(size + 1);
	if (p)
		memcpy(p, str, size + 1);
	return p;
}

#ifdef __WIN32__
wchar_t *
wimlib_wcsdup(const wchar_t *str)
{
	size_t size;
	wchar_t *p;

	size = wcslen(str);
	p = MALLOC((size + 1) * sizeof(wchar_t));
	if (p)
		memcpy(p, str, (size + 1) * sizeof(wchar_t));
	return p;
}
#endif

extern void
xml_set_memory_allocator(void *(*malloc_func)(size_t),
			 void (*free_func)(void *),
			 void *(*realloc_func)(void *, size_t));
#endif

WIMLIBAPI int
wimlib_set_memory_allocator(void *(*malloc_func)(size_t),
			    void (*free_func)(void *),
			    void *(*realloc_func)(void *, size_t))
{
#ifdef ENABLE_CUSTOM_MEMORY_ALLOCATOR
	wimlib_malloc_func  = malloc_func  ? malloc_func  : malloc;
	wimlib_free_func    = free_func    ? free_func    : free;
	wimlib_realloc_func = realloc_func ? realloc_func : realloc;

	xml_set_memory_allocator(wimlib_malloc_func, wimlib_free_func,
				 wimlib_realloc_func);
	return 0;
#else
	ERROR("Cannot set custom memory allocator functions:");
	ERROR("wimlib was compiled with the --without-custom-memory-allocator "
	      "flag");
	return WIMLIB_ERR_UNSUPPORTED;
#endif
}

static bool seeded = false;

static void
seed_random()
{
	srand(time(NULL) * getpid());
	seeded = true;
}

/* Fills @n characters pointed to by @p with random alphanumeric characters. */
void
randomize_char_array_with_alnum(tchar p[], size_t n)
{
	if (!seeded)
		seed_random();
	while (n--) {
		int r = rand() % 62;
		if (r < 26)
			*p++ = r + 'a';
		else if (r < 52)
			*p++ = r - 26 + 'A';
		else
			*p++ = r - 52 + '0';
	}
}

/* Fills @n bytes pointer to by @p with random numbers. */
void
randomize_byte_array(u8 *p, size_t n)
{
	if (!seeded)
		seed_random();
	while (n--)
		*p++ = rand();
}

const tchar *
path_basename_with_len(const tchar *path, size_t len)
{
	const tchar *p = &path[len] - 1;

	/* Trailing slashes. */
	while (1) {
		if (p == path - 1)
			return T("");
		if (*p != T('/'))
			break;
		p--;
	}

	while ((p != path - 1) && *p != T('/'))
		p--;

	return p + 1;
}

/* Like the basename() function, but does not modify @path; it just returns a
 * pointer to it. */
const tchar *
path_basename(const tchar *path)
{
	return path_basename_with_len(path, tstrlen(path));
}

/*
 * Returns a pointer to the part of @path following the first colon in the last
 * path component, or NULL if the last path component does not contain a colon.
 */
const tchar *
path_stream_name(const tchar *path)
{
	const tchar *base = path_basename(path);
	const tchar *stream_name = tstrchr(base, T(':'));
	if (!stream_name)
		return NULL;
	else
		return stream_name + 1;
}

u64
get_wim_timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return timeval_to_wim_timestamp(tv);
}

void
wim_timestamp_to_str(u64 timestamp, tchar *buf, size_t len)
{
	struct tm tm;
	time_t t = wim_timestamp_to_unix(timestamp);
	gmtime_r(&t, &tm);
	tstrftime(buf, len, T("%a %b %d %H:%M:%S %Y UTC"), &tm);
}

void
zap_backslashes(tchar *s)
{
	if (s) {
		while (*s != T('\0')) {
			if (*s == T('\\'))
				*s = T('/');
			s++;
		}
	}
}
