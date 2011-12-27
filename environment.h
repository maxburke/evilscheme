#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#define NUM_ENTRIES_PER_FRAGMENT 16

struct symbol_table_entry_t
{
    struct object_t *symbol;
    struct object_t *object;
};

struct symbol_table_fragment_t
{
    struct symbol_table_entry_t entries[NUM_ENTRIES_PER_FRAGMENT];
    struct symbol_table_fragment_t *next_fragment;
};

struct environment_t
{
    struct environment_t *parent_environment;
    struct symbol_table_fragment_t *symbol_table_fragment;
};

extern struct environment_t *global_environment;

void
create_environment(struct environment_t **environment_ptr);

struct object_t **
get_bound_location(struct environment_t *environment, struct object_t *symbol, int recurse);

struct object_t **
bind(struct environment_t *environment, struct object_t *args);

void
initialize_global_environment(struct environment_t *environment);

#endif
