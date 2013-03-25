/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef READ_H
#define READ_H

struct object_t;
struct environment_t;

struct object_t
read(struct environment_t *environment, int num_args, struct object_t *object);

#endif

