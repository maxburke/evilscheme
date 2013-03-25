/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_USER_H
#define EVIL_USER_H

#include <stddef.h>

/*
 * The user is responsible for implementing these functions.
 */

void *
evil_aligned_alloc(size_t alignment, size_t size);

void
evil_aligned_free(void *ptr);

void
evil_print(const char *format, ...);

void
evil_debug_print(const char *format, ...);

#endif
