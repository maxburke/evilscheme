/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_SLIST_H
#define EVIL_SLIST_H

/*
 * Singly linked list used for storage of the intermediate tokens.
 */
struct slist_t
{
    struct slist_t *next;
};

void
slist_link(struct slist_t *from, struct slist_t *to);

struct slist_t *
slist_reverse(struct slist_t *head);

#endif
