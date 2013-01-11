#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "object.h"
#include "runtime.h"
#include "slist.h"
#include "vm.h"

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
    struct environment_t *environment;
};

static void
initialize_compiler_context(
        struct compiler_context_t *context,
        struct environment_t *environment,
        struct object_t *args)
{
    memset(context, 0, sizeof(struct compiler_context_t));
    context->args = args;
    context->environment = environment;
}

static void
destroy_compiler_context(struct compiler_context_t *context)
{
    discard_pool(&context->pool);
}

struct instruction_t
{
    struct slist_t link;

    unsigned char insn;
    size_t size;
    struct instruction_t *reloc;

    union data
    {
        unsigned char u1;
                 char s1;
        unsigned short u2;
                 short s2;
        unsigned int u4;
                 int s4;
        float f4;
        uint64_t u8;
        int64_t s8;
        double f8;
        char string[1];
    } data;
};

static struct instruction_t *
compile_if(struct compiler_context_t *context, struct object_t *body)
{
    struct object_t *test;
    struct object_t *consequent;
    struct object_t *alternate;
    struct object_t *temp;

    temp = CDR(body);
    test = CAR(temp);

    temp = CDR(temp);
    consequent = CAR(temp);
    alternate = CDR(temp);

    UNUSED(context);
    
    if (alternate != empty_pair)
    {
    }

    return NULL;
}

static struct instruction_t *
compile_literal(struct compiler_context_t *context, struct object_t *literal)
{
    struct instruction_t *instruction;
    size_t extra_size;

    assert(literal->tag_count.count >= 1);

    extra_size = literal->tag_count.count - 1;
    instruction = pool_alloc(&context->pool, sizeof(struct instruction_t) + extra_size);

    switch (literal->tag_count.tag)
    {
        case TAG_BOOLEAN:
            instruction->insn = OPCODE_LDIMM_1_BOOL;
            break;
        case TAG_SYMBOL:
            instruction->insn = OPCODE_LDIMM_8_SYMBOL;
            instruction->size = 8;
            instruction->data.u8 = (char)literal->value.symbol_hash;
            break;
        case TAG_CHAR:
            instruction->insn = OPCODE_LDIMM_1_CHAR;
            instruction->size = 1;
            instruction->data.s1 = (char)literal->value.fixnum_value;
            break;
        case TAG_FIXNUM:
            {
                int64_t n;

                n = literal->value.fixnum_value;

                if (n >= -128 && n <= 127)
                {
                    instruction->insn = OPCODE_LDIMM_1_FIXNUM;
                    instruction->size = 1;
                    instruction->data.s1 = (char)n;
                }
                else if (n >= -2147483648LL && n <= 2147483647)
                {
                    instruction->insn = OPCODE_LDIMM_4_FIXNUM;
                    instruction->size = 4;
                    instruction->data.s4 = (int)n;
                }
                else
                {
                    instruction->insn = OPCODE_LDIMM_8_FIXNUM;
                    instruction->size = 8;
                    instruction->data.s8 = n;
                }
            }
            break;
        case TAG_FLONUM:
            {
                float f;
                double d;

                d = literal->value.flonum_value;
                f = (float)d;

                if (d == (double)f)
                {
                    instruction->insn = OPCODE_LDIMM_4_FLONUM;
                    instruction->size = 4;
                    instruction->data.f4 = f;
                }
                else
                {
                    instruction->insn = OPCODE_LDIMM_8_FLONUM;
                    instruction->size = 8;
                    instruction->data.f8 = d;
                }
            }
            break;
        case TAG_STRING:
            instruction->insn = OPCODE_LDSTR;
            instruction->size = literal->tag_count.count + 1;
            memcpy(&instruction->data.string, literal->value.string_value, instruction->size);
            break;
        default:
            BREAK();
            break;
    }

    return instruction;
}

static struct instruction_t *
compile_load_arg(struct compiler_context_t *context, int arg_index)
{
    struct instruction_t *instruction;

    /*
     * TODO: If we need to support more than 256 arguments the LDARG_X
     * instruction will need to be augmented.
     */
    assert(arg_index < 256 && arg_index >= 0);
    
    instruction = pool_alloc(&context->pool, sizeof(struct instruction_t));
    instruction->insn = OPCODE_LDARG_X;
    instruction->size = 1;
    instruction->data.u1 = (unsigned char)arg_index;

    return instruction;
}

static struct instruction_t *
compile_symbol_load(struct compiler_context_t *context, struct object_t *symbol)
{
    struct instruction_t *ldimm_symbol;
    struct instruction_t *get_bound_location;

    assert(symbol->tag_count.tag == TAG_SYMBOL);

    ldimm_symbol = pool_alloc(&context->pool, sizeof(struct instruction_t));
    ldimm_symbol->insn = OPCODE_LDIMM_8_SYMBOL;
    ldimm_symbol->size = 8;
    ldimm_symbol->data.u8 = symbol->value.symbol_hash;

    get_bound_location = pool_alloc(&context->pool, sizeof(struct instruction_t)); 
    get_bound_location->insn = OPCODE_GET_BOUND_LOCATION;
    get_bound_location->size = 0;
    get_bound_location->link.next = &ldimm_symbol->link;

    return get_bound_location;
}

#define SYMBOL_IF 0x8325f07b4eb2a24

static struct instruction_t *
compile_form(struct compiler_context_t *context, struct object_t *body)
{
    struct object_t *symbol_object;
    uint64_t symbol_hash;
    int arg_index;

    UNUSED(context);

    symbol_object = CAR(body);
    symbol_hash = symbol_object->value.symbol_hash;

    if (symbol_object->tag_count.tag != TAG_SYMBOL)
        return compile_literal(context, symbol_object);

    assert(symbol_object->tag_count.tag == TAG_SYMBOL);

    switch (symbol_hash)
    {
        case SYMBOL_IF:
            return compile_if(context, body);
        default:
            skim_print("** %s (0x%" PRIx64 ") **\n", 
                    find_symbol_name(context->environment, symbol_hash),
                    symbol_hash);
    }

    arg_index = get_arg_index(context->args, symbol_hash);

    if (arg_index != UNKNOWN_ARG)
    {
        return compile_load_arg(context, arg_index);
    }

    return compile_symbol_load(context, symbol_object);
}

struct object_t *
lambda(struct environment_t *environment, struct object_t *lambda_body)
{
    struct object_t *args;
    struct object_t *body;
    struct slist_t root;
    struct compiler_context_t context;
    
    args = CAR(lambda_body);
    body = CAR(CDR(lambda_body));
    root.next = NULL;

    initialize_compiler_context(&context, environment, args);

    for (body = CDR(lambda_body); body != empty_pair; body = CDR(body))
    {
        struct instruction_t *instruction;
        
        instruction = compile_form(&context, CAR(body));
        slist_splice(&instruction->link, &root);
        slist_splice(&root, &instruction->link);
    }

    assert(root.next != NULL);

    slist_reverse(&root);
    assemble(root);

    destroy_compiler_context(&context);

    return NULL;
}


