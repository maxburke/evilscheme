#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "slist.h"
#include "vm.h"

#define UNKNOWN_ARG -1

#define DEFAULT_POOL_CHUNK_SIZE 4096

#define allocate_instruction(context) pool_alloc(&context->pool, sizeof(struct instruction_t))

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

    unsigned char opcode;
    int offset;
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
compile_form(struct compiler_context_t *context, struct instruction_t *next, struct object_t *body);

static struct instruction_t *
compile_symbol_load(struct compiler_context_t *context, struct instruction_t *next, struct object_t *symbol);

static struct instruction_t *
find_before(struct instruction_t *start, struct instruction_t *target)
{
    struct slist_t *end;
    struct slist_t *i;

    end = &target->link;
    
    for (i = &start->link; i->next != end; i = i->next)
        ;

    return (struct instruction_t *)i;
}

static struct instruction_t *
compile_if(struct compiler_context_t *context, struct instruction_t *next, struct object_t *body)
{
    struct object_t *test_form;
    struct object_t *consequent_form;
    struct object_t *alternate_form;
    struct object_t *temp;

    struct instruction_t *test_code;
    struct instruction_t *consequent_code;
    struct instruction_t *alternate_code;
    struct instruction_t *cond_br;
    struct instruction_t *br;
    struct instruction_t *nop;
    struct instruction_t *cond_br_target;

    test_form = CAR(body);
    temp = CDR(body);
    consequent_form = CAR(temp);
    alternate_form = CAR(CDR(temp));

    test_code = compile_form(context, next, test_form);

    cond_br = allocate_instruction(context);
    cond_br->opcode = OPCODE_COND_BRANCH;
    cond_br->link.next = &test_code->link;

    br = allocate_instruction(context);
    br->opcode = OPCODE_BRANCH;

    nop = allocate_instruction(context);
    nop->opcode = OPCODE_NOP;

    if (alternate_form == empty_pair)
    {
        consequent_code = compile_form(context, br, consequent_form);

        /*
         * The conditional branch jumps to the beginning of the consequent code
         * block, but because everything's all craaaazy backwards here we need
         * to first find the first instruction. This *might* be easier with
         * a doubly linked list...
         */
        cond_br_target = find_before(consequent_code, br);
        cond_br->reloc = cond_br_target;

        /*
         * Since there's no alternate, we emit a nop that is the branch target
         * if the conditional is not taken. This can be eliminated by a pass
         * through the bytecode at a later date if desired.
         */
        br->link.next = &cond_br->link;
        br->reloc = nop;
        nop->link.next = &consequent_code->link;

        return nop;
    }

    alternate_code = compile_form(context, cond_br, alternate_form);
    consequent_code = compile_form(context, alternate_code, consequent_form);
    br->link.next = &alternate_code->link;
    br->reloc = nop;

    cond_br_target = find_before(consequent_code, alternate_code);
    cond_br->reloc = cond_br_target;

    nop->link.next = &consequent_code->link;

    return nop;
}

static struct instruction_t *
compile_plus(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct instruction_t *insn;
    struct object_t *arg;
    int i;

    insn = next;
    arg = args;
    i = 0;

    for (arg = args; arg != empty_pair; arg = CDR(arg), ++i)
    {
        insn = compile_form(context, insn, CAR(arg));

        if (i >= 1)
        {
            struct instruction_t *plus;

            plus = allocate_instruction(context);
            plus->opcode = OPCODE_ADD;
            plus->link.next = &insn->link;
            insn = plus;
        }
    }

    return insn;
}

static struct instruction_t *
compile_arg_eval(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct instruction_t *evaluated_args;

    if (args == empty_pair)
        return next;

    evaluated_args = compile_arg_eval(context, next, CDR(args));

    return compile_form(context, evaluated_args, CAR(args));
}

static struct instruction_t *
compile_call(struct compiler_context_t *context, struct instruction_t *next, struct object_t *function, struct object_t *args)
{
    struct instruction_t *evaluated_args;
    struct instruction_t *function_symbol;
    struct instruction_t *call;

    /*
     * Optimization opportunity -- tail calls to self can be replaced with 
     * branch to beginning of function. The difficulty will be detecting if
     * we are branching to ourself. This will not be possible if we're compiling
     * the form (define foo (lambda (x) ...))) because the slot foo can later be
     * rebound, but if we're compiling (define (foo x) ...) I think it would be
     * acceptable behavior to perform this optimization.
     *
     * TODO: Once the parser recognizes (define (foo x) ...), add this in.
     */

    evaluated_args = compile_arg_eval(context, next, args);
    function_symbol = compile_symbol_load(context, evaluated_args, function);

    /*
     * All function calls are emitted as normal calls here. Later we run a pass
     * over the bytecode that converts calls to tailcalls if possible.
     */

    call = allocate_instruction(context);
    call->link.next = &function_symbol->link;
    call->opcode = OPCODE_CALL;

    return call;
}

static struct instruction_t *
compile_nullp(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct object_t *expression_form;
    struct instruction_t *expression;
    struct instruction_t *ldempty;
    struct instruction_t *cmp_eq;

    expression_form = CAR(args);
    expression = compile_form(context, next, expression_form);
    ldempty = allocate_instruction(context);
    ldempty->opcode = OPCODE_LDEMPTY;
    ldempty->link.next = &expression->link;

    cmp_eq = allocate_instruction(context);
    cmp_eq->opcode = OPCODE_CMP_EQUAL;
    cmp_eq->link.next = &ldempty->link;

    return cmp_eq;
}

static struct instruction_t *
compile_literal(struct compiler_context_t *context, struct instruction_t *next, struct object_t *literal)
{
    struct instruction_t *instruction;
    size_t extra_size;

    assert(literal->tag_count.count >= 1);

    extra_size = literal->tag_count.count - 1;
    instruction = pool_alloc(&context->pool, sizeof(struct instruction_t) + extra_size);
    instruction->link.next = &next->link;

    switch (literal->tag_count.tag)
    {
        case TAG_BOOLEAN:
            instruction->opcode = OPCODE_LDIMM_1_BOOL;
            instruction->data.s1 = (char)literal->value.fixnum_value;
            break;
        case TAG_SYMBOL:
            instruction->opcode = OPCODE_LDIMM_8_SYMBOL;
            instruction->size = 8;
            instruction->data.u8 = (char)literal->value.symbol_hash;
            break;
        case TAG_CHAR:
            instruction->opcode = OPCODE_LDIMM_1_CHAR;
            instruction->size = 1;
            instruction->data.s1 = (char)literal->value.fixnum_value;
            break;
        case TAG_FIXNUM:
            {
                int64_t n;

                n = literal->value.fixnum_value;

                if (n >= -128 && n <= 127)
                {
                    instruction->opcode = OPCODE_LDIMM_1_FIXNUM;
                    instruction->size = 1;
                    instruction->data.s1 = (char)n;
                }
                else if (n >= -2147483648LL && n <= 2147483647)
                {
                    instruction->opcode = OPCODE_LDIMM_4_FIXNUM;
                    instruction->size = 4;
                    instruction->data.s4 = (int)n;
                }
                else
                {
                    instruction->opcode = OPCODE_LDIMM_8_FIXNUM;
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
                    instruction->opcode = OPCODE_LDIMM_4_FLONUM;
                    instruction->size = 4;
                    instruction->data.f4 = f;
                }
                else
                {
                    instruction->opcode = OPCODE_LDIMM_8_FLONUM;
                    instruction->size = 8;
                    instruction->data.f8 = d;
                }
            }
            break;
        case TAG_STRING:
            instruction->opcode = OPCODE_LDSTR;
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
compile_load_arg(struct compiler_context_t *context, struct instruction_t *next, int arg_index)
{
    struct instruction_t *instruction;

    /*
     * TODO: If we need to support more than 256 arguments the LDARG_X
     * instruction will need to be augmented.
     */
    assert(arg_index < 256 && arg_index >= 0);
    
    instruction = allocate_instruction(context);
    instruction->opcode = OPCODE_LDARG_X;
    instruction->size = 1;
    instruction->data.u1 = (unsigned char)arg_index;
    instruction->link.next = &next->link;

    return instruction;
}

static struct instruction_t *
compile_symbol_load(struct compiler_context_t *context, struct instruction_t *next, struct object_t *symbol)
{
    struct instruction_t *ldimm_symbol;
    struct instruction_t *get_bound_location;

    assert(symbol->tag_count.tag == TAG_SYMBOL);

    ldimm_symbol = allocate_instruction(context);
    ldimm_symbol->opcode = OPCODE_LDIMM_8_SYMBOL;
    ldimm_symbol->size = 8;
    ldimm_symbol->data.u8 = symbol->value.symbol_hash;
    ldimm_symbol->link.next = &next->link;

    get_bound_location = allocate_instruction(context); 
    get_bound_location->opcode = OPCODE_GET_BOUND_LOCATION;
    get_bound_location->size = 0;
    get_bound_location->link.next = &ldimm_symbol->link;

    return get_bound_location;
}

#define SYMBOL_IF 0x8325f07b4eb2a24
#define SYMBOL_NULLP 0xb4d24b59678288cd
#define SYMBOL_PLUS 0xaf63bd4c8601b7f4

static struct instruction_t *
compile_form(struct compiler_context_t *context, struct instruction_t *next, struct object_t *body)
{
    struct object_t *symbol_object;
    uint64_t symbol_hash;
    int arg_index;

    symbol_object = body;

    if (symbol_object->tag_count.tag == TAG_PAIR)
    {
        struct object_t *function_symbol;
        struct object_t *function_args;
        uint64_t function_hash;

        function_symbol = CAR(symbol_object);
        function_args = CDR(symbol_object);

        assert(function_symbol->tag_count.tag == TAG_SYMBOL);
        function_hash = function_symbol->value.symbol_hash;

        switch (function_hash)
        {
            case SYMBOL_IF:
                return compile_if(context, next, function_args);
            case SYMBOL_NULLP:
                return compile_nullp(context, next, function_args);
            case SYMBOL_PLUS:
                return compile_plus(context, next, function_args);
            default:
                /*
                 * The function/procedure isn't one that is handled by the
                 * compiler so we need to emit a call to it.
                 * TODO: determine if this can be a tailcall.
                 */
skim_print("** %s (0x%" PRIx64 ") **\n", 
        find_symbol_name(context->environment, function_hash),
        function_hash);
                return compile_call(context, next, function_symbol, function_args);
        }
    }

    if (symbol_object->tag_count.tag != TAG_SYMBOL)
        return compile_literal(context, next, symbol_object);

    symbol_hash = symbol_object->value.symbol_hash;
    arg_index = get_arg_index(context->args, symbol_hash);

    if (arg_index != UNKNOWN_ARG)
    {
        return compile_load_arg(context, next, arg_index);
    }

    return compile_symbol_load(context, next, symbol_object);
}

static size_t
calculate_bytecode_size_and_offsets(struct slist_t *root)
{
    struct slist_t *i;
    size_t size;

    size = 0;

    for (i = root; i != NULL; i = i->next)
    {
        struct instruction_t *insn;

        insn = (struct instruction_t *)i;
        insn->offset = size;
        size += 1 + insn->size;
    }

    assert(size < 65536);
    return size;
}

union convert_two_t
{
    unsigned char bytes[2];
    unsigned short u2;
    short s2;
};

union convert_four_t
{
    unsigned char bytes[4];
    float f4;
    unsigned int u4;
    int s4;
};

union convert_eight_t
{
    unsigned char bytes[8];
    double f8;
    uint64_t u8;
    int64_t s8;
};

static struct object_t *
assemble(struct environment_t *environment, struct instruction_t *root)
{
    struct slist_t *insns;
    struct slist_t *i;
    struct procedure_t *procedure;
    void *mem;
    unsigned char *bytes;
    size_t num_bytes;
    size_t idx;
    UNUSED(environment);

    /*
     * This must be the last function called as it destructively alters the
     * instruction chain.
     */

    insns = slist_reverse(&root->link);
    idx = 0;
    num_bytes = calculate_bytecode_size_and_offsets(insns);
    mem = gc_alloc(environment->heap, TAG_PROCEDURE, num_bytes);
    procedure = mem;
    bytes = procedure->byte_code;

    for (i = insns; i != NULL; i = i->next)
    {
        struct instruction_t *insn;
        unsigned char opcode;
        size_t size;

        insn = (struct instruction_t *)i;
        opcode = insn->opcode;
        size = insn->size;
        bytes[idx++] = opcode;

        switch (opcode)
        {
            case OPCODE_BRANCH:
            case OPCODE_COND_BRANCH:
                {
                    int offset;
                    int target_offset;
                    int diff;
                    union convert_two_t c2;

                    /*
                     * We add 1 to the offset of the current instruction 
                     * because this one accounts for the PC's offset after
                     * it has decoded the opcode.
                     */
                    offset = insn->offset + 1;
                    target_offset = insn->reloc->offset;
                    diff = target_offset - offset;

                    assert(diff >= -32768 && diff < 32767);

                    c2.s2 = (short)diff;
                    memcpy(&bytes[idx], c2.bytes, 2);
                    idx += 2;
                }
                break;
            case OPCODE_LDIMM_1_BOOL:
            case OPCODE_LDIMM_1_CHAR:
            case OPCODE_LDIMM_1_FIXNUM:
                bytes[idx++] = (unsigned char)insn->data.s1;
                break;
            case OPCODE_LDARG_X:
                bytes[idx++] = insn->data.u1;
                break;
            case OPCODE_LDIMM_4_FIXNUM:
                {
                    union convert_four_t c4;
                    c4.s4 = insn->data.s4;
                    memcpy(&bytes[idx], c4.bytes, 4);
                    idx += 4;
                }
                break;
            case OPCODE_LDIMM_4_FLONUM:
                {
                    union convert_four_t c4;
                    c4.f4 = insn->data.f4;
                    memcpy(&bytes[idx], c4.bytes, 4);
                    idx += 4;
                }
                break;
            case OPCODE_LDIMM_8_FIXNUM:
                {
                    union convert_eight_t c8;
                    c8.s8 = insn->data.s8;
                    memcpy(&bytes[idx], c8.bytes, 8);
                    idx += 8;
                }
                break;
            case OPCODE_LDIMM_8_FLONUM:
                {
                    union convert_eight_t c8;
                    c8.f8 = insn->data.f8;
                    memcpy(&bytes[idx], c8.bytes, 8);
                    idx += 8;
                }
                break;
            case OPCODE_LDIMM_8_SYMBOL:
                {
                    union convert_eight_t c8;
                    c8.u8 = insn->data.s8;
                    memcpy(&bytes[idx], c8.bytes, 8);
                    idx += 8;
                }
                break;
            case OPCODE_LDSTR:
                memcpy(&bytes[idx], insn->data.string, size);
                idx += size;
                break;
        }
    }

    return (struct object_t *)mem;
}

static struct instruction_t *
add_return_insn(struct compiler_context_t *context, struct instruction_t *root)
{
    struct instruction_t *ret;

    ret = allocate_instruction(context);
    ret->opcode = OPCODE_RETURN;
    ret->link.next = &root->link;

    return ret;
}

static inline int
is_branch(struct instruction_t *insn)
{
    unsigned char opcode;

    opcode = insn->opcode;

    return opcode == OPCODE_BRANCH || opcode == OPCODE_COND_BRANCH;
}

static void
eradicate_nop(struct instruction_t *root, 
        struct instruction_t *nop,
        struct instruction_t *prev)
{
    struct slist_t *i;

    for (i = &root->link; i != NULL; i = i->next)
    {
        struct instruction_t *insn;

        insn = (struct instruction_t *)i;
        if (!is_branch(insn))
            continue;

        if (insn->reloc != nop)
            continue;

        insn->reloc = prev;
    }
}

static void
collapse_nops(struct instruction_t *root)
{
    struct slist_t *i;
    struct instruction_t *prev;

    prev = NULL;

    for (i = &root->link; i != NULL; i = i->next)
    {
        struct instruction_t *insn;

        insn = (struct instruction_t *)i;

        if (insn->opcode == OPCODE_NOP)
        {
            assert(prev != NULL);
            eradicate_nop(root, insn, prev);
            prev->link.next = insn->link.next;
        }

        prev = insn;
    }
}

static void
promote_tailcalls(struct compiler_context_t *context, struct instruction_t *root)
{
    UNUSED(context);
    UNUSED(root);

    /*
     * This gets a TODO. This function needs to determine if a particular call 
     * is actually a tail call and, if it is, change its call opcode into the
     * tailcall opcode.
     */
}

struct object_t *
disassemble_procedure(struct object_t *procedure, const char *name)
{
    UNUSED(procedure);

    skim_print("hello!!!! %s\n", name);
    return empty_pair;
}

struct object_t *
disassemble(struct environment_t *environment, struct object_t *args)
{
    struct object_t *symbol;
    struct object_t **slot;
    struct object_t *function;
    const char *name;

print(environment, args);
    symbol = eval(environment, CAR(args));

    assert(symbol->tag_count.tag == TAG_SYMBOL);
    slot = get_bound_location(environment, symbol, 1);
    assert(slot != NULL);

    function = *slot;
    assert(function != NULL);

    name = find_symbol_name(environment, symbol->value.symbol_hash);

    switch (function->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            skim_print("%s: <special function>\n", name);
            return function;
        case TAG_PROCEDURE:
            return disassemble_procedure(function, name);
        default:
            BREAK();
            break;
    }

    return empty_pair;
}

struct object_t *
lambda(struct environment_t *environment, struct object_t *lambda_body)
{
    struct object_t *args;
    struct object_t *body;
    struct object_t *procedure;
    struct instruction_t *root;
    struct compiler_context_t context;
    
    args = CAR(lambda_body);
    body = CAR(CDR(lambda_body));
    root = NULL;

    initialize_compiler_context(&context, environment, args);

    for (body = CDR(lambda_body); body != empty_pair; body = CDR(body))
    {
        root = compile_form(&context, root, CAR(body));
    }

    root = add_return_insn(&context, root);
    collapse_nops(root);
    promote_tailcalls(&context, root);

    procedure = assemble(environment, root);

    destroy_compiler_context(&context);

    return procedure;
}


