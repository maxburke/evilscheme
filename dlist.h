/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_DLIST_H
#define EVIL_DLIST_H

/*
 * Doubly linked list.
 */
struct dlist_t
{
    struct dlist_t *next;
    struct dlist_t *prev;
};

void
dlist_initialize(struct dlist_t *list);

struct dlist_t *
dlist_pop(struct dlist_t *list);

void
dlist_push(struct dlist_t *list, struct dlist_t *item);

void
dlist_remove(struct dlist_t *item);

#endif
