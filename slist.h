#ifndef SLIST_H
#define SLIST_H

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
slist_splice(struct slist_t *from, struct slist_t *to);

struct slist_t *
slist_reverse(struct slist_t *head);

#endif
