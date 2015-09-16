/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "evil_scheme.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

struct evil_object_t empty_pair_storage = { { TAG_PAIR, 0, 0 }, { 0 } };
struct evil_object_t *empty_pair = &empty_pair_storage;

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

static struct evil_object_t
symbol_to_string(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args);

static struct evil_object_t
string_to_symbol(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args);

void
environment_initialize(struct evil_environment_t *environment)
{
    struct special_function_initializer_t
    {
        const char *name;
        evil_special_function_t function;
        int num_args;
    };

    static struct special_function_initializer_t initializers[] =
    {
        { "read", evil_read, 1 },
        { "eval", evil_eval, 1 },
        { "print", evil_print, 1 },
        { "cons", evil_cons, 2 },
        { "define", evil_define, 2 },
        { "lambda", evil_lambda, 1 },
        { "apply", evil_apply, VARIADIC },
        { "vector", evil_vector, VARIADIC },
        { "make-vector", evil_make_vector, VARIADIC },
        { "vector-length", evil_vector_ref, 1 },
        { "vector-ref", evil_vector_ref, 2 },
        { "vector-fill!", evil_vector_ref, 2 },
        { "disassemble", evil_disassemble, 1 },
        { "string->symbol", string_to_symbol, 1 },
        { "symbol->string", symbol_to_string, 1 }
    };
    #define NUM_INITIALIZERS (sizeof initializers / sizeof initializers[0])
    size_t i;

    for (i = 0; i < NUM_INITIALIZERS; ++i)
    {
        struct evil_object_t *place;
        struct evil_object_t symbol;
        struct evil_object_t *procedure;
        struct evil_object_t *procedure_base;

        struct evil_object_t function;

        symbol.tag_count.tag = TAG_SYMBOL;
        symbol.tag_count.flag = 0;
        symbol.tag_count.count = 1;
        symbol.value.symbol_hash = register_symbol_from_string(environment, initializers[i].name);

        function.tag_count.tag = TAG_EXTERNAL_FUNCTION;
        function.tag_count.flag = 0;
        function.tag_count.count = 1;
        function.value.special_function_value = initializers[i].function;

        procedure = gc_alloc_vector(environment->heap, FIELD_LOCALS);
        procedure->tag_count.tag = TAG_SPECIAL_FUNCTION;
        procedure_base = VECTOR_BASE(procedure);

        procedure_base[FIELD_ENVIRONMENT] = make_ref((struct evil_object_t *)environment);
        procedure_base[FIELD_LEXICAL_ENVIRONMENT] = environment->lexical_environment;
        procedure_base[FIELD_NUM_ARGS] = make_fixnum_object(initializers[i].num_args);
        procedure_base[FIELD_NUM_LOCALS] = make_fixnum_object(0);
        procedure_base[FIELD_NUM_FN_LOCALS] = make_fixnum_object(0);
        procedure_base[FIELD_CODE] = function;

        place = bind(environment, environment->lexical_environment, symbol);
        *place = make_ref(procedure);
    }
}

struct evil_environment_t *
evil_environment_create(void *stack, size_t stack_size, void *heap_mem, size_t heap_size)
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
    struct evil_environment_t *env;
    struct evil_object_t *lexical_environment_ptr;

    heap = gc_create(heap_mem, heap_size);

    /*
     * Originally the top level environment was created in the GC heap but I
     * think this is probably not the best idea because if this object were to
     * be moved it would cause a lot of pain.
     */
    env = evil_aligned_alloc(sizeof(void *), sizeof(struct evil_environment_t));
    memset(env, 0, sizeof(struct evil_environment_t));

    env->tag_count.tag = TAG_ENVIRONMENT;
    env->tag_count.count = 1;
    env->stack_bottom = stack;
    env->stack_top = (struct evil_object_t *)((char *)stack + stack_size) - 1;
    env->stack_ptr = env->stack_top;
    memset(stack, 0, stack_size);

    env->heap = heap;

    lexical_environment_ptr = gc_alloc_vector(heap, FIELD_LEX_ENV_NUM_FIELDS);
    VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_PARENT_ENVIRONMENT] = make_empty_ref();
    VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT] = make_empty_ref();
    env->lexical_environment = make_ref(lexical_environment_ptr);

    environment_initialize(env);
    gc_set_environment(heap, env);

    return env;
}

static void
evil_destroy_hasn_internment_pages(struct symbol_hash_internment_page_t *page)
{
    if (page == NULL)
    {
        return;
    }

    evil_destroy_hasn_internment_pages(page->next);
    free(page);
}

static void
evil_destroy_string_internment_pages(struct symbol_string_internment_page_t *page)
{
    if (page == NULL)
    {
        return;
    }

    evil_destroy_string_internment_pages(page->next);
    free(page);
}

void
evil_environment_destroy(struct evil_environment_t *environment)
{
    gc_destroy(environment->heap);

    evil_destroy_hasn_internment_pages(environment->symbol_names.hash_internment_page_base);
    evil_destroy_string_internment_pages(environment->symbol_names.string_internment_page_base);
    evil_aligned_free(environment);
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
find_symbol_name(struct evil_environment_t *environment, uint64_t key)
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
intern_string(struct evil_environment_t *environment, const char *bytes, size_t num_bytes)
{
    struct symbol_string_internment_page_t *i;

    for (i = environment->symbol_names.string_internment_page_base; i != NULL; i = i->next)
    {
        char *string;
        size_t ii;

        if (i->available_bytes < (num_bytes + 1))
            continue;

        string = i->top;
        i->top += (num_bytes + 1);
        i->available_bytes -= (num_bytes + 1);

        /*
         * All symbols are interned lower-case as the Scheme is case
         * insensitive.
         */
        for (ii = 0; ii < num_bytes; ++ii)
        {
            string[ii] = (char)tolower(bytes[ii]);
        }

        string[num_bytes] = 0;

        return string;
    }

    i = calloc(DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE, 1);
    assert(i != NULL);
    i->next = environment->symbol_names.string_internment_page_base;
    environment->symbol_names.string_internment_page_base = i;
    i->top = i->data;
    i->available_bytes = DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE
        - offsetof(struct symbol_string_internment_page_t, data);

    return intern_string(environment, bytes, num_bytes);
}

static void
intern_hash(struct evil_environment_t *environment, uint64_t hash, const char *string)
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

    i = calloc(DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE, 1);
    assert(i != NULL);
    i->next = environment->symbol_names.hash_internment_page_base;
    environment->symbol_names.hash_internment_page_base = i;
    i->num_entries = 0;
    i->available_slots = (DEFAULT_SYMBOL_INTERNMENT_PAGE_SIZE
        - offsetof(struct symbol_string_internment_page_t, data)) / sizeof(struct interned_hash_entry_t);

    intern_hash(environment, hash, string);
}

static uint64_t
register_symbol_impl(struct evil_environment_t *environment, const char *bytes, size_t num_bytes)
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
register_symbol_from_string(struct evil_environment_t *environment, const char *string)
{
    return register_symbol_impl(environment, string, strlen(string));
}

uint64_t
register_symbol_from_bytes(struct evil_environment_t *environment, const void *bytes, size_t num_bytes)
{
    return register_symbol_impl(environment, bytes, num_bytes);
}

static struct evil_object_t
symbol_to_string(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    uint64_t hash;
    const char *symbol_string;
    struct evil_object_t *string_object;
    size_t symbol_string_length;

    UNUSED(lexical_environment);

    assert(num_args == 1);
    assert(args->tag_count.tag == TAG_SYMBOL);

    hash = args->value.symbol_hash;
    symbol_string = find_symbol_name(environment, hash);

    /*
     * I don't think this is allowed to return non-null. If we have the symbol
     * object then at one time the string should have been interned.
     */
    assert(symbol_string != NULL);
    symbol_string_length = strlen(symbol_string);

    string_object = gc_alloc(environment->heap, TAG_STRING, symbol_string_length);
    memmove(&string_object->value.string_value, symbol_string, symbol_string_length + 1);

    return make_ref(string_object);
}

static struct evil_object_t
string_to_symbol(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *string_object;
    uint64_t hash;
    struct evil_object_t hash_object;

    UNUSED(lexical_environment);

    assert(num_args == 1);
    assert(args->tag_count.tag == TAG_REFERENCE);

    string_object = deref(args);
    assert(string_object->tag_count.tag == TAG_STRING);

    hash = register_symbol_from_string(environment, string_object->value.string_value);
    hash_object.tag_count.tag = TAG_SYMBOL;
    hash_object.tag_count.flag = 0;
    hash_object.tag_count.count = 1;
    hash_object.value.symbol_hash = hash;

    return hash_object;
}

