/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <stddef.h>

#include "dlist.h"

void
dlist_initialize(struct dlist_t *list)
{
    list->next = list;
    list->prev = list;
}

struct dlist_t *
dlist_pop(struct dlist_t *list)
{
    struct dlist_t *item;

    if (list->next == list)
    {
        return NULL;
    }

    item = list->next;
    dlist_remove(item);

    return item;
}

void
dlist_push(struct dlist_t *list, struct dlist_t *item)
{
    assert(list != NULL);
    assert(item->next == NULL);
    assert(item->prev == NULL);
    assert(list->next != NULL);

    list->next->prev = item;
    item->next = list->next;
    item->prev = list;
    list->next = item;
}

void
dlist_remove(struct dlist_t *item)
{
    assert(item != NULL);
    assert(item->next != NULL);
    assert(item->prev != NULL);

    item->next->prev = item->prev;
    item->prev->next = item->next;

    item->next = NULL;
    item->prev = NULL;
}

