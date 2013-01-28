/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <stddef.h>

#include "slist.h"

/*
 * Helper function to add a new link to the specified slist.
 */
void 
slist_link(struct slist_t *from, struct slist_t *to)
{
    from->next = to;
}

struct slist_t *
slist_reverse(struct slist_t *head)
{
    struct slist_t *last = NULL;

    while (head != NULL) 
    {
        struct slist_t *temp = head->next;
        head->next = last;
        last = head;
        head = temp;
    }

    return last;
}


