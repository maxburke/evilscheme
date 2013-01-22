#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "read.h"
#include "object.h"
#include "runtime.h"

struct object_t empty_pair_storage = { { TAG_PAIR, 0, 0 }, { 0 } };
struct object_t *empty_pair = &empty_pair_storage;

#define DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE 4096

struct interned_hash_entry_t
{
    uint64_t hash;
    const char *string;
};

struct symbol_hash_internment_page_t
{
    struct symbol_hash_internment_page_t *next;
    size_t num_entries;
    size_t available_slots;
    struct interned_hash_entry_t data[1];
};

struct symbol_string_internment_page_t
{
    struct symbol_string_internment_page_t *next;
    char *top;
    size_t available_bytes;
    char data[1];
};

void
environment_initialize(struct environment_t *environment)
{
    struct special_function_initializer_t
    {
        const char *name;
        special_function_t function;
    };

    static struct special_function_initializer_t initializers[] = 
    {
        { "quote", quote },
        { "read", read },
        { "eval", eval },
        { "print", print },
        { "set!", set },
        { "cons", cons },
        { "car", car },
        { "cdr", cdr },
        { "define", define },
        { "lambda", lambda },
        { "vector", vector },
        { "disassemble", disassemble },
    };
    #define NUM_INITIALIZERS (sizeof initializers / sizeof initializers[0])
    size_t i;

    for (i = 0; i < NUM_INITIALIZERS; ++i)
    {
        struct object_t *p0;
        struct object_t *place;
        struct object_t symbol;
        struct object_t function;
        struct object_t arg_ref;

        symbol.tag_count.tag = TAG_SYMBOL;
        symbol.tag_count.flag = 0;
        symbol.tag_count.count = 1;
        symbol.value.symbol_hash = register_symbol_from_string(environment, initializers[i].name);

        function.tag_count.tag = TAG_SPECIAL_FUNCTION;
        function.tag_count.flag = 0;
        function.tag_count.count = 1;
        function.value.special_function_value = initializers[i].function;

        p0 = gc_alloc(environment->heap, TAG_PAIR, 0);
        *CAR(p0) = symbol;
        *CDR(p0) = make_empty_ref();
        arg_ref = make_ref(p0);

        place = bind(environment, &arg_ref);
        *place = function;
    }
}

struct environment_t *
environment_create(void *stack, size_t stack_size, void *heap_mem, size_t heap_size)
{
    struct heap_t *heap;

    /*
     * This bit of casting magic is to make the error "initialization from
     * incompatible pointer types" go away. The environment and object pointer
     * types are technically incompatible but they all go through the same
     * GC allocation path anyways. One other fix could be to have gc_alloc
     * return void* but this should help prevent unintended auto-casts of
     * newly allocated values.
     */
    void *env_mem;
    struct environment_t *env;

    heap = gc_create(heap_mem, heap_size);
    env_mem = gc_alloc(heap, TAG_ENVIRONMENT, 0);
    env = env_mem;

    env->stack_bottom = stack;
    env->stack_top = (struct object_t *)((char *)stack + stack_size) - 1;
    env->stack_ptr = env->stack_top;
    env->heap = heap;
    env->symbol_table_fragment = NULL;

    environment_initialize(env);
    return env;
}

/*
 * Symbols are a value type that have their string representation stored
 * hashed. The hashing algorithm below is the FNV-1 hash algorithm.
 */

#define PRIME UINT64_C(1099511628211)

static uint64_t
symbol_hash_bytes(const void *bytes, size_t num_bytes)
{
    const char *ptr;
    uint64_t hash;
    size_t i;

    ptr = bytes;
    hash = UINT64_C(14695981039346656037);

    for (i = 0; i < num_bytes; ++i)
    {
        hash = (hash * PRIME) ^ (uint64_t)(unsigned char)(tolower(ptr[i]));
    }

    return hash;
}

static int
interned_hash_entry_comparer(const void *a_ptr, const void *b_ptr)
{
    const struct interned_hash_entry_t *a;
    const struct interned_hash_entry_t *b;
    uint64_t a_key;
    uint64_t b_key;

    a = a_ptr;
    b = b_ptr;
    a_key = a->hash;
    b_key = b->hash;

    return (a_key > b_key) ? 1 : ((a_key < b_key) ? -1 : 0);
}

const char *
find_symbol_name(struct environment_t *environment, uint64_t key)
{
    struct symbol_hash_internment_page_t *i;

    for (i = environment->symbol_names.hash_internment_page_base; i != NULL; i = i->next)
    {
        struct interned_hash_entry_t *entry;
        
        entry = bsearch(&key, 
                i->data, 
                i->num_entries, 
                sizeof(struct interned_hash_entry_t),
                interned_hash_entry_comparer);

        if (entry)
            return entry->string;
    }

    return NULL;
}

static const char *
intern_string(struct environment_t *environment, const char *bytes, size_t num_bytes)
{
    struct symbol_string_internment_page_t *i;

    for (i = environment->symbol_names.string_internment_page_base; i != NULL; i = i->next)
    {
        char *string;

        if (i->available_bytes < (num_bytes + 1))
            continue;

        string = i->top;
        i->top += (num_bytes + 1);
        i->available_bytes -= (num_bytes + 1);

        memmove(string, bytes, num_bytes);
        string[num_bytes] = 0;

        return string;
    }

    i = calloc(1, DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE);
    i->next = environment->symbol_names.string_internment_page_base;
    environment->symbol_names.string_internment_page_base = i;
    i->top = i->data;
    i->available_bytes = DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE 
        - offsetof(struct symbol_string_internment_page_t, data);

    return intern_string(environment, bytes, num_bytes);
}

static void 
intern_hash(struct environment_t *environment, uint64_t hash, const char *string)
{
    struct symbol_hash_internment_page_t *i;

    for (i = environment->symbol_names.hash_internment_page_base; i != NULL; i = i->next)
    {
        size_t num_entries;
        size_t available_slots;
        struct interned_hash_entry_t *entry;

        available_slots = i->available_slots;

        if (i->available_slots == 0)
            continue;

        num_entries = i->num_entries;
        entry = &i->data[num_entries];

        entry->hash = hash;
        entry->string = string;

        ++num_entries;
        i->num_entries = num_entries;
        i->available_slots = available_slots - 1;

        qsort(i->data, num_entries, sizeof(struct interned_hash_entry_t), interned_hash_entry_comparer);

        return;
    }

    i = calloc(1, DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE);
    i->next = environment->symbol_names.hash_internment_page_base;
    environment->symbol_names.hash_internment_page_base = i;
    i->num_entries = 0;
    i->available_slots = (DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE 
        - offsetof(struct symbol_string_internment_page_t, data)) / sizeof(struct interned_hash_entry_t);

    intern_hash(environment, hash, string);
}


static uint64_t
register_symbol_impl(struct environment_t *environment, const char *bytes, size_t num_bytes)
{
    uint64_t hash;
    const char *interned_string;

    hash = symbol_hash_bytes(bytes, num_bytes);

    if (find_symbol_name(environment, hash) != NULL)
        return hash;

    interned_string = intern_string(environment, bytes, num_bytes);
    intern_hash(environment, hash, interned_string);

    return hash;
}

uint64_t
register_symbol_from_string(struct environment_t *environment, const char *string)
{
    return register_symbol_impl(environment, string, strlen(string));
}

uint64_t
register_symbol_from_bytes(struct environment_t *environment, const void *bytes, size_t num_bytes)
{
    return register_symbol_impl(environment, bytes, num_bytes);
}

