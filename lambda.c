#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "object.h"
#include "vm.h"

#define SYMBOL_IF 0x8325f07b4eb2a24
#define SYMBOL_NULL 0xb4d24b59678288cd
#define SYMBOL_PLUS 0xaf63bd4c8601b7f4

#define UNKNOWN_ARG -1

#define DEFAULT_POOL_CHUNK_SIZE 4096

struct memory_pool_chunk_t
{
    struct memory_pool_chunk_t *next;
    char *top;
    char *end;
    char mem[1];
};

struct memory_pool_t
{
    struct memory_pool_chunk_t *head;
};

static void *
pool_alloc(struct memory_pool_t *pool, size_t size)
{
    struct memory_pool_chunk_t *i;
    size_t max_alloc_size;

    max_alloc_size = DEFAULT_POOL_CHUNK_SIZE - offsetof(struct memory_pool_chunk_t, mem);
    size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    assert(size <= max_alloc_size);

    for (i = pool->head; i != NULL; i = i->next)
    {
        size_t pool_room = i->end - i->top;

        if (pool_room >= size)
        {
            void *mem = i->top;
            i->top += size;

            return mem;
        }
    }
        
    i = calloc(1, DEFAULT_POOL_CHUNK_SIZE);
    i->top = &i->mem[0];
    i->end = i->top + max_alloc_size;
    i->next = pool->head;
    pool->head = i;

    return pool_alloc(pool, size);
}

static void
discard_pool_chunk(struct memory_pool_chunk_t *chunk)
{
    if (chunk != NULL)
    {
        discard_pool_chunk(chunk->next);
        free(chunk);
    }
}

static void
discard_pool(struct memory_pool_t *pool)
{
    discard_pool_chunk(pool->head);
}

static int
get_arg_index(struct object_t *args, uint64_t hash)
{
    struct object_t *i;
    int idx;

    idx = 0;

    for (i = args; i != empty_pair; i = CDR(i), ++idx)
    {
        struct object_t *arg_symbol;
        
        arg_symbol = CAR(i);
        assert(arg_symbol->tag_count.tag == TAG_SYMBOL);
        if (arg_symbol->value.symbol_hash == hash)
            return idx;
    }

    return UNKNOWN_ARG;
}

struct compiler_context_t
{
    struct memory_pool_t pool;
    struct object_t *args;
};

static void
initialize_compiler_context(struct compiler_context_t *context, struct object_t *args)
{
    memset(context, 0, sizeof(struct compiler_context_t));
    context->args = args;
}

static void
destroy_compiler_context(struct compiler_context_t *context)
{
    discard_pool(&context.pool);
}

static void
compile_form(struct compiler_context_t *context, struct object_t *form)
{
}

struct object_t *
lambda(struct environment_t *environment, struct object_t *lambda_body)
{
    struct object_t *args;
    struct object_t *body;

    struct compiler_context_t compiler_context;

    UNUSED(environment);
    
    args = CAR(lambda_body);
    body = CAR(CDR(lambda_body));
    initialize_compiler_context(&context, args);

/*
    for (i = args; CAR(i) != empty_pair; i = CDR(i))
    {
        print(environment, i);
    }
    BREAK(); 
*/

    destroy_compiler_context(&context);
    return NULL;
}


