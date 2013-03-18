/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <setjmp.h>
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

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define SYMBOL_IF       0x8325f07b4eb2a24
#define SYMBOL_ADD      0xaf63bd4c8601b7f4
#define SYMBOL_SUB      0xaf63bd4c8601b7f2
#define SYMBOL_MUL      0xaf63bd4c8601b7f5
#define SYMBOL_DIV      0xaf63bd4c8601b7f0
#define SYMBOL_EQ       0xaf63bd4c8601b7e2
#define SYMBOL_LT       0xaf63bd4c8601b7e3
#define SYMBOL_GT       0xaf63bd4c8601b7e1
#define SYMBOL_LE       0x8328c07b4eb7684
#define SYMBOL_GE       0x8328a07b4eb736e
#define SYMBOL_EQUALP   0xeacd8283b4334dba
#define SYMBOL_NULLP    0xb4d24b59678288cd
#define SYMBOL_FIRST    0xc0de9a9b8ec0e479
#define SYMBOL_REST     0x9ab4817ea75b3deb
#define SYMBOL_LAMBDA   0xdb4485ae65d0c568
#define SYMBOL_SET      0x92eb577ea331d824
#define SYMBOL_LET      0xd8b7ad186b906050
#define SYMBOL_LETSTAR  0xd07b707ec653a7da
#define SYMBOL_LETREC   0x4c663d13ff171fa
#define SYMBOL_DEFINE   0xf418d40f59d3f9a4
#define SYMBOL_QUOTE    0xf6be341a7b50a73
#define SYMBOL_BEGIN    0x8facd8be36ce1840

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

            memset(mem, 0, size);

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

struct stack_slot_t
{
    struct slist_t link;
    uint64_t symbol_hash;
    struct instruction_t *initializer;
    short index;
};

struct function_local_t
{
    struct slist_t link;

    /*
     * TODO: Should this be an object handle instead of a pointer?
     */
    struct object_t *object;
};

struct compiler_context_t
{
    struct compiler_context_t *previous_context;
    jmp_buf context_state;
    struct memory_pool_t pool;
    int num_args;
    struct environment_t *environment;
    struct stack_slot_t *stack_slots;
    int max_stack_slots;
    int num_fn_locals;
    struct function_local_t *locals;
};

static void
create_slots_for_args(
        struct compiler_context_t *context,
        struct object_t *args,
        int slot_index)
{
    /*
     * This function creates the stack slot objects for all the function's
     * arguments. It does so by recursing down the list of arguments to the
     * end and then pushing them on from the end back to the beginning.
     */

    struct stack_slot_t *stack_slot;
    struct object_t *arg_symbol;

    if (args == empty_pair)
        return;

    create_slots_for_args(context, CDR(args), slot_index + 1);

    arg_symbol = CAR(args);
    assert(arg_symbol->tag_count.tag == TAG_SYMBOL);

    stack_slot = pool_alloc(&context->pool, sizeof(struct stack_slot_t));
    stack_slot->symbol_hash = arg_symbol->value.symbol_hash;
    stack_slot->index = (short)slot_index;
    stack_slot->link.next = &context->stack_slots->link;
    stack_slot->initializer = NULL;

    context->stack_slots = stack_slot;
}

static void
initialize_compiler_context(
        struct compiler_context_t *context,
        struct environment_t *environment,
        struct object_t *args,
        struct compiler_context_t *previous_context)
{
    struct object_t *i;
    int num_args;

    num_args = 0;
    for (i = args; i != empty_pair; i = CDR(i), ++num_args)
        ;

    memset(context, 0, sizeof(struct compiler_context_t));
    create_slots_for_args(context, args, 0);

    context->environment = environment;
    context->num_args = num_args;
    context->previous_context = previous_context;
}

static void
destroy_compiler_context(struct compiler_context_t *context)
{
    discard_pool(&context->pool);
}

static struct stack_slot_t *
get_stack_slot(struct stack_slot_t *slots, uint64_t hash)
{
    while (slots != NULL)
    {
        if (slots->symbol_hash == hash)
        {
            return slots;
        }

        slots = (struct stack_slot_t *)slots->link.next;
    }

    return NULL;
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
compile_get_bound_location(struct compiler_context_t *context, struct instruction_t *next, struct object_t *symbol);

static struct instruction_t *
compile_load(struct compiler_context_t *context, struct instruction_t *next);

static struct instruction_t *
compile_lambda(struct compiler_context_t *incoming_context, struct instruction_t *next, struct object_t *lambda_body);

static struct instruction_t *
compile_load_function_local(struct compiler_context_t *context, struct instruction_t *next, struct object_t *object);

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
    cond_br->size = 2;
    cond_br->link.next = &test_code->link;

    br = allocate_instruction(context);
    br->opcode = OPCODE_BRANCH;
    br->size = 2;

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
    br->link.next = &alternate_code->link;
    br->reloc = nop;
    consequent_code = compile_form(context, br, consequent_form);

    cond_br_target = find_before(consequent_code, br);
    cond_br->reloc = cond_br_target;

    nop->link.next = &consequent_code->link;

    return nop;
}

static struct instruction_t *
compile_add(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
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

static inline int
count_parameters(struct object_t *args)
{
    struct object_t *arg;
    int i;

    i = 0;
    for (arg = args; arg != empty_pair; arg = CDR(arg), ++i)
        ;

    return i;
}

static struct instruction_t *
compile_sub(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct instruction_t *insn;
    struct object_t *arg;
    int i;
    int num_parameters;

    insn = next;
    arg = args;
    i = 0;

    num_parameters = count_parameters(args);
    assert(num_parameters >= 1);

    if (num_parameters == 1)
    {
        /*
         * (- x) should return x negated. This pushes a zero onto the 
         * operation stack and calls sub as normal, effectively translating 
         * this operation to (- 0 x).
         */
        struct instruction_t *zero;

        zero = allocate_instruction(context);
        zero->opcode = OPCODE_LDIMM_1_FIXNUM;
        zero->size = 1;
        zero->link.next = &insn->link;

        insn = zero;

        /*
         * i is incremented here so that we ensure that the SUB opcode is
         * emitted below even if we have only one argument.
         */
        ++i;
    }

    for (arg = args; arg != empty_pair; arg = CDR(arg), ++i)
    {
        insn = compile_form(context, insn, CAR(arg));

        if (i >= 1)
        {
            struct instruction_t *plus;

            plus = allocate_instruction(context);
            plus->opcode = OPCODE_SUB;
            plus->link.next = &insn->link;
            insn = plus;
        }
    }

    return insn;
}

static struct instruction_t *
compile_mul(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct instruction_t *insn;
    struct object_t *arg;
    int i;
    int num_parameters;

    insn = next;
    arg = args;
    i = 0;

    num_parameters = count_parameters(args);
    assert(num_parameters >= 1);

    if (num_parameters == 0)
    {
        /*
         * (*) evaluates to 1.
         */
        struct instruction_t *one;

        one = allocate_instruction(context);
        one->opcode = OPCODE_LDIMM_1_FIXNUM;
        one->data.s1 = 1;
        one->size = 1;
        one->link.next = &insn->link;

        return one;
    }
    else if (num_parameters == 1)
    {
        /*
         * (* x) evalutates to x.
         */
        return compile_form(context, insn, CAR(arg));
    }

    for (arg = args; arg != empty_pair; arg = CDR(arg), ++i)
    {
        insn = compile_form(context, insn, CAR(arg));

        if (i >= 1)
        {
            struct instruction_t *plus;

            plus = allocate_instruction(context);
            plus->opcode = OPCODE_MUL;
            plus->link.next = &insn->link;
            insn = plus;
        }
    }

    return insn;
}

static struct instruction_t *
compile_div(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct instruction_t *insn;
    struct object_t *arg;
    int i;
    int num_parameters;

    insn = next;
    arg = args;
    i = 0;

    num_parameters = count_parameters(args);
    assert(num_parameters >= 1);

    if (num_parameters == 1)
    {
        /*
         * (/ x) evaluates to the reciprocal of x by pushing a 1 onto the
         * stack and calling DIV, in otherwords (/ x) <=> (/ 1 x)
         */
        struct instruction_t *one;

        one = allocate_instruction(context);
        one->opcode = OPCODE_LDIMM_1_FIXNUM;
        one->size = 1;
        one->data.s1 = 1;
        one->link.next = &insn->link;

        insn = one;

        /*
         * i is incremented here so that we ensure that the DIV opcode is
         * emitted below even if we only have one argument.
         */
        ++i;
    }

    for (arg = args; arg != empty_pair; arg = CDR(arg), ++i)
    {
        insn = compile_form(context, insn, CAR(arg));

        if (i >= 1)
        {
            struct instruction_t *plus;

            plus = allocate_instruction(context);
            plus->opcode = OPCODE_DIV;
            plus->link.next = &insn->link;
            insn = plus;
        }
    }

    return insn;
}

#define COMPILE_COMPARE(COMPARE, OPCODE) \
    static struct instruction_t *                                                                               \
    compile_ ## COMPARE(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)  \
    {                                                                                                           \
        struct instruction_t *insn;                                                                             \
        struct instruction_t *lhs;                                                                              \
        struct instruction_t *rhs;                                                                              \
        struct object_t *lhs_form;                                                                              \
        struct object_t *rhs_form;                                                                              \
                                                                                                                \
        lhs_form = args;                                                                                        \
        rhs_form = CDR(args);                                                                                   \
                                                                                                                \
        rhs = compile_form(context, next, CAR(rhs_form));                                                       \
        lhs = compile_form(context, rhs, CAR(lhs_form));                                                        \
                                                                                                                \
        insn = allocate_instruction(context);                                                                   \
        insn->opcode = OPCODE;                                                                                  \
        insn->link.next = &lhs->link;                                                                           \
                                                                                                                \
        return insn;                                                                                            \
    }                                                                                                           \

COMPILE_COMPARE(equalp, OPCODE_CMP_EQUAL)
COMPILE_COMPARE(eq, OPCODE_CMPN_EQ)
COMPILE_COMPARE(lt, OPCODE_CMPN_LT)
COMPILE_COMPARE(gt, OPCODE_CMPN_GT)
COMPILE_COMPARE(le, OPCODE_CMPN_LE)
COMPILE_COMPARE(ge, OPCODE_CMPN_GE)

static struct instruction_t *
compile_arg_eval(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args, int *num_args)
{
    struct instruction_t *evaluated_args;

    if (args == empty_pair)
        return next;

    ++*num_args;
    evaluated_args = compile_arg_eval(context, next, CDR(args), num_args);

    return compile_form(context, evaluated_args, CAR(args));
}

static struct instruction_t *
compile_call(struct compiler_context_t *context, struct instruction_t *next, struct object_t *function, struct object_t *args)
{
    struct instruction_t *evaluated_args;
    struct instruction_t *function_symbol;
    struct instruction_t *bound_location;
    struct instruction_t *call;
    int num_args;

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

    num_args = 0;
    evaluated_args = compile_arg_eval(context, next, args, &num_args);
    bound_location = compile_get_bound_location(context, evaluated_args, function);
    function_symbol = compile_load(context, bound_location);

    /*
     * All function calls are emitted as normal calls here. Later we run a pass
     * over the bytecode that converts calls to tailcalls if possible.
     *
     * We embed the number of arguments passed into this particular invocation
     * so that at runtime we can verify that the arity is correct for the
     * function call.
     */
    assert(num_args < 65536);

    call = allocate_instruction(context);
    call->link.next = &function_symbol->link;
    call->data.u2 = (unsigned short)num_args;
    call->size = 2;
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
            instruction->data.u8 = literal->value.symbol_hash;
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
                else if (d == 0.0 || d == -0.0)
                {
                    instruction->opcode = OPCODE_LDIMM_1_FLONUM;
                    instruction->size = 1;
                    instruction->data.u1 = 0;
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
compile_load_slot(struct compiler_context_t *context, struct instruction_t *next, struct stack_slot_t *slot)
{
    struct instruction_t *instruction;

    instruction = allocate_instruction(context);
    instruction->opcode = OPCODE_LDSLOT_X;
    instruction->size = 2;
    instruction->data.s2 = slot->index;
    instruction->link.next = &next->link;

    return instruction;
}

static struct instruction_t *
compile_get_bound_location(struct compiler_context_t *context, struct instruction_t *next, struct object_t *symbol)
{
    struct instruction_t *get_bound_location;

    assert(symbol->tag_count.tag == TAG_SYMBOL);

    get_bound_location = allocate_instruction(context); 
    get_bound_location->opcode = OPCODE_GET_BOUND_LOCATION;
    get_bound_location->size = 8;
    get_bound_location->data.u8 = symbol->value.symbol_hash;
    get_bound_location->link.next = &next->link;

    return get_bound_location;
}

static struct instruction_t *
compile_load(struct compiler_context_t *context, struct instruction_t *next)
{
    struct instruction_t *load;

    load = allocate_instruction(context);
    load->opcode = OPCODE_LOAD;
    load->link.next = &next->link;

    return load;
}

static struct instruction_t *
compile_store(struct compiler_context_t *context, struct instruction_t *next)
{
    struct instruction_t *store;

    store = allocate_instruction(context);
    store->opcode = OPCODE_STORE;
    store->link.next = &next->link;

    return store;
}

static struct instruction_t *
compile_store_slot(struct compiler_context_t *context, struct instruction_t *next, int slot_index)
{
    struct instruction_t *store;

    assert(slot_index >= -32768 && slot_index < 32768);

    store = allocate_instruction(context);
    store->opcode = OPCODE_STSLOT_X;
    store->size = 2;
    store->link.next = &next->link;
    store->data.s2 = (short)slot_index;

    return store;
}

static int
count_active_stack_slots(struct stack_slot_t *slots)
{
    int i;

    for (i = 0; slots != NULL && slots->index < -2; slots = (struct stack_slot_t *)slots->link.next, ++i)
        ;

    return i;
}

static struct instruction_t *
compile_let(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    int i;
    int num_slots;
    int current_active_stack_slots;
    struct object_t *binding_list;
    struct object_t *body;
    struct stack_slot_t *slot;

    /*
     * This code evaluates the let statements in program order and so is able
     * to provide the same implementation for both let and let*.
     */

    num_slots = 0;
    current_active_stack_slots = count_active_stack_slots(context->stack_slots);
    body = CAR(CDR(args));

    for (binding_list = CAR(args); binding_list != empty_pair; binding_list = CDR(binding_list), ++num_slots)
    {
        struct object_t *binding_pair;
        struct object_t *symbol;
        struct stack_slot_t *stack_slot;
        struct instruction_t *initializer;
        struct instruction_t *initializer_store;
        int slot_index;

        binding_pair = CAR(binding_list);
        symbol = CAR(binding_pair);

        assert(symbol->tag_count.tag == TAG_SYMBOL);

        if (CDR(symbol) != empty_pair)
        {
            struct object_t *initializer_form;

            /*
             * If the code provides an initializer this code compiles the
             * initialization form and pushes the result to the stack. The
             * resulting stack value is used as the slot for the newly 
             * created "variable".
             */

            initializer_form = CAR(CDR(binding_pair));
            initializer = compile_form(context, next, initializer_form);
        }
        else
        {
            /*
             * Same deal as above but we initialize the slot to '() if no
             * initializer form is specialized.
             */
            initializer = allocate_instruction(context);
            initializer->opcode = OPCODE_LDEMPTY;
            initializer->link.next = &next->link;
        }

        slot_index = current_active_stack_slots++;
        assert(slot_index >= 0 && slot_index < 65536);

        /*
         * The stack is arranged, from high address to low:
         * [arg 1][arg 0][return][chain][first slot]
         *    1      0       -1     -2       -3
         * This bit of strange math below calculates the stack slot:
         */
        slot_index = -((short)slot_index + 3);

        initializer_store = compile_store_slot(context, initializer, slot_index);

        stack_slot = pool_alloc(&context->pool, sizeof(struct stack_slot_t));
        stack_slot->symbol_hash = symbol->value.symbol_hash;
        stack_slot->initializer = initializer;
        stack_slot->link.next = &context->stack_slots->link;
        stack_slot->index = (short)slot_index;

        context->stack_slots = stack_slot;
        next = initializer_store;
    }

    context->max_stack_slots = MAX(context->max_stack_slots, current_active_stack_slots);

    /*
     * Compile body of let here.
     */
    next = compile_form(context, next, body);
    
    /*
     * Collapse scopes so that the symbols are no longer visible after control
     * leaves this let block.
     */

    slot = context->stack_slots;

    for (i = 0; i < num_slots; ++i)
    {
        slot = (struct stack_slot_t *)slot->link.next;
    }

    context->stack_slots = slot;

    return next;
}

static struct instruction_t *
compile_define(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct object_t *symbol_form;
    struct object_t *value_form;
    struct instruction_t *value;
    struct instruction_t *symbol;
    struct instruction_t *define_symbol_location;
    struct instruction_t *define_function;
    struct instruction_t *call;
    struct object_t define_symbol;

    symbol_form = CAR(args);
    value_form = CAR(CDR(args));

    value = compile_form(context, next, value_form);

    assert(symbol_form->tag_count.tag == TAG_SYMBOL);

    symbol = allocate_instruction(context);
    symbol->opcode = OPCODE_LDIMM_8_SYMBOL;
    symbol->size = 8;
    symbol->data.u8 = symbol_form->value.symbol_hash;
    symbol->link.next = &value->link;

    define_symbol.tag_count.tag = TAG_SYMBOL;
    define_symbol.tag_count.flag = 0;
    define_symbol.tag_count.count = 1;
    define_symbol.value.symbol_hash = SYMBOL_DEFINE;

    define_symbol_location = compile_get_bound_location(context, symbol, &define_symbol);
    define_function = compile_load(context, define_symbol_location);

    call = allocate_instruction(context);
    call->data.u2 = 2;
    call->size = 2;
    call->opcode = OPCODE_CALL;
    call->link.next = &define_function->link;

    return call;
}

static struct instruction_t *
compile_quote(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct object_t *quote_form;

    quote_form = CAR(args);

    return compile_load_function_local(context, next, quote_form); 
}

static struct instruction_t *
compile_begin(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct object_t *body;
    struct instruction_t *root;

    root = next;

    for (body = args; body != empty_pair; body = CDR(body))
    {
        root = compile_form(context, root, CAR(body));
    }

    return root;
}

static struct instruction_t *
compile_set(struct compiler_context_t *context, struct instruction_t *next, struct object_t *args)
{
    struct object_t *place_form;
    struct object_t *value_form;
    struct instruction_t *value;

    place_form = CAR(args);
    value_form = CAR(CDR(args));

    value = compile_form(context, next, value_form);

    if (place_form->tag_count.tag == TAG_PAIR)
    {
        struct instruction_t *place;
        struct instruction_t *set;

        /*
         * Evaluate the place. This should always evaluate to an inner 
         * reference. I think. I hope.
         */
        place = compile_form(context, value, place_form);

        set = allocate_instruction(context);
        set->opcode = OPCODE_SET;
        set->link.next = &place->link;

        return set;
    }

    if (place_form->tag_count.tag == TAG_SYMBOL)
    {
        uint64_t hash;
        struct stack_slot_t *stack_slot;
        struct instruction_t *store;

        hash = place_form->value.symbol_hash;
        stack_slot = get_stack_slot(context->stack_slots, hash);

        if (stack_slot == NULL)
        {
            struct instruction_t *bound_location;

            bound_location = compile_get_bound_location(context, value, place_form);
            store = compile_store(context, bound_location);
        }
        else
        {
            store = compile_store_slot(context, value, stack_slot->index);
        }

        return store;
    }

    /*
     * The place must either be an expression that evaluates to an
     * inner reference, or it must be a variable. If we're here, 
     * something has gone terribly wrong.
     */
    BREAK();

    return NULL;
}

#define COMPILE_PAIR_ACCESSOR(ACCESSOR, INDEX)                                                                      \
    static struct instruction_t *                                                                                   \
    compile_ ## ACCESSOR(struct compiler_context_t *context, struct instruction_t *next, struct object_t *symbol)   \
    {                                                                                                               \
        struct instruction_t *index;                                                                                \
        struct instruction_t *ref;                                                                                  \
        struct instruction_t *load;                                                                                 \
        struct instruction_t *list;                                                                                 \
                                                                                                                    \
        list = compile_form(context, next, CAR(symbol));                                                            \
                                                                                                                    \
        index = allocate_instruction(context);                                                                      \
        index->opcode = OPCODE_LDIMM_1_FIXNUM;                                                                      \
        index->size = 1;                                                                                            \
        index->data.s1 = INDEX;                                                                                     \
        index->link.next = &list->link;                                                                             \
                                                                                                                    \
        ref = allocate_instruction(context);                                                                        \
        ref->opcode = OPCODE_MAKE_REF;                                                                              \
        ref->link.next = &index->link;                                                                              \
                                                                                                                    \
        load = allocate_instruction(context);                                                                       \
        load->opcode = OPCODE_LOAD;                                                                                 \
        load->link.next = &ref->link;                                                                               \
                                                                                                                    \
        return load;                                                                                                \
    }

COMPILE_PAIR_ACCESSOR(first, 0)
COMPILE_PAIR_ACCESSOR(rest, 1)

static struct instruction_t *
compile_form(struct compiler_context_t *context, struct instruction_t *next, struct object_t *body)
{
    struct object_t *symbol_object;
    uint64_t symbol_hash;
    struct stack_slot_t *slot;
    struct instruction_t *bound_location;

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
            case SYMBOL_ADD:
                return compile_add(context, next, function_args);
            case SYMBOL_SUB:
                return compile_sub(context, next, function_args);
            case SYMBOL_MUL:
                return compile_mul(context, next, function_args);
            case SYMBOL_DIV:
                return compile_div(context, next, function_args);
            case SYMBOL_EQ:
                return compile_eq(context, next, function_args);
            case SYMBOL_LT:
                return compile_lt(context, next, function_args);
            case SYMBOL_GT:
                return compile_gt(context, next, function_args);
            case SYMBOL_LE:
                return compile_le(context, next, function_args);
            case SYMBOL_GE:
                return compile_ge(context, next, function_args);
            case SYMBOL_EQUALP:
                return compile_equalp(context, next, function_args);
            case SYMBOL_NULLP:
                return compile_nullp(context, next, function_args);
            case SYMBOL_FIRST:
                return compile_first(context, next, function_args);
            case SYMBOL_REST:
                return compile_rest(context, next, function_args);
            case SYMBOL_LAMBDA:
                return compile_lambda(context, next, function_args);
            case SYMBOL_SET:
                return compile_set(context, next, function_args);
            case SYMBOL_LET:
            case SYMBOL_LETSTAR:
                return compile_let(context, next, function_args);
            case SYMBOL_DEFINE:
                return compile_define(context, next, function_args);
            case SYMBOL_QUOTE:
                return compile_quote(context, next, function_args);
            case SYMBOL_BEGIN:
                return compile_begin(context, next, function_args);
            default:
                /*
                 * The function/procedure isn't one that is handled by the
                 * compiler so we need to emit a call to it.
                 */
                evil_debug_print("** %s (0x%" PRIx64 ") **\n", 
                    find_symbol_name(context->environment, function_hash),
                    function_hash);
                return compile_call(context, next, function_symbol, function_args);
        }
    }

    if (symbol_object->tag_count.tag != TAG_SYMBOL)
        return compile_literal(context, next, symbol_object);

    symbol_hash = symbol_object->value.symbol_hash;

    slot = get_stack_slot(context->stack_slots, symbol_hash);

    if (slot != NULL)
    {
        return compile_load_slot(context, next, slot);
    }

    bound_location = compile_get_bound_location(context, next, symbol_object);
    return compile_load(context, bound_location);
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

static struct object_t *
assemble(struct environment_t *environment, struct compiler_context_t *context, struct instruction_t *root)
{
    struct slist_t *local_slots;
    struct slist_t *insns;
    struct slist_t *i;
    struct evil_object_handle_t byte_code_ptr;
    struct object_t *procedure;
    struct object_t *byte_code;
    unsigned char *bytes;
    size_t num_bytes;
    size_t idx;
    size_t fn_local_idx;
    struct object_t *procedure_base;

    /*
     * This must be the last function called as it destructively alters the
     * instruction chain.
     */

    insns = slist_reverse(&root->link);
    idx = 0;
    num_bytes = calculate_bytecode_size_and_offsets(insns);

    byte_code = gc_alloc(environment->heap, TAG_STRING, num_bytes);

    /*
     * If this gc_alloc call triggers a gc we can lose the byte_code reference
     * above which would be bad (or entertaining) and cause many problems.
     *
     * GC TODO: Add the pointer referenced above to a managed pointer structure.
     */
    byte_code_ptr = evil_create_object_handle(environment->heap, byte_code);
    procedure = gc_alloc_vector(environment->heap, FIELD_LOCALS + context->num_fn_locals);
    procedure->tag_count.tag = TAG_PROCEDURE;

    byte_code = byte_code_ptr.object;

    bytes = (unsigned char *)byte_code->value.string_value;
    procedure_base = VECTOR_BASE(procedure);
    procedure_base[FIELD_NUM_ARGS] = make_fixnum_object(context->num_args);
    procedure_base[FIELD_NUM_LOCALS] = make_fixnum_object(context->max_stack_slots);
    procedure_base[FIELD_NUM_FN_LOCALS] = make_fixnum_object(context->num_fn_locals);
    procedure_base[FIELD_CODE] = make_ref(byte_code);

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
            case OPCODE_CALL:
            case OPCODE_TAILCALL:
            case OPCODE_STSLOT_X:
            case OPCODE_LDSLOT_X:
                {
                    union convert_two_t c2;

                    c2.s2 = insn->data.s2;
                    memcpy(&bytes[idx], c2.bytes, 2);
                    idx += 2;
                }
                break;
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
                    offset = insn->offset + 3;
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
            case OPCODE_LDIMM_1_FLONUM:
                bytes[idx++] = (unsigned char)insn->data.u1;
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
            case OPCODE_GET_BOUND_LOCATION:
            case OPCODE_LDIMM_8_SYMBOL:
                {
                    union convert_eight_t c8;
                    c8.u8 = insn->data.u8;
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

    /*
     * If this assert fires then chances are we've forgotten to set the size
     * field of an instruction instance.
     */
    assert(idx == num_bytes);

    /*
     * Insert the function local objects into the slotsin the function object.
     */
    local_slots = slist_reverse(&context->locals->link);

    for (fn_local_idx = 0; local_slots != NULL; local_slots = local_slots->next, ++fn_local_idx)
    {
        struct function_local_t *local;
        
        local = (struct function_local_t *)local_slots;

        procedure_base[FIELD_LOCALS + fn_local_idx] = make_ref(local->object);
    }
    
    /*
     * The object handle is no longer needed at this point.
     */
    evil_destroy_object_handle(environment->heap, byte_code_ptr);

    return procedure;
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
eliminate_branch_to_return(struct instruction_t *root)
{
    struct slist_t *i;

    for (i = &root->link; i != NULL; i = i->next)
    {
        struct instruction_t *insn;

        insn = (struct instruction_t *)i;

        if (is_branch(insn))
        {
            if (insn->reloc->opcode == OPCODE_RETURN)
            {
                /*
                 * As this code writes over an existing opcode with a RETURN,
                 * we need to explicitly set the opcode size here to zero.
                 */
                insn->opcode = OPCODE_RETURN;
                insn->size = 0;
                insn->reloc = NULL;
            }
        }
    }
}

static struct instruction_t *
promote_tailcalls(struct instruction_t *root)
{
    struct slist_t *i;
    struct instruction_t *prev;

    prev = NULL;

    for (i = &root->link; i != NULL; i = i->next)
    {
        struct instruction_t *insn;

        insn = (struct instruction_t *)i;
        if (insn->opcode == OPCODE_RETURN)
        {
            struct instruction_t *next;

            next = (struct instruction_t *)i->next;

            if (next->opcode == OPCODE_CALL)
            {
                next->opcode = OPCODE_TAILCALL;

                /*
                 * This used to eliminate the return instruction that occurred
                 * after a tailcall but I think it needs to be here still. For
                 * VM bytecode functions this is an unreachable instruction
                 * but it will be necessary for non-bytecode (ie, external C)
                 * functions.

                if (prev != NULL)
                {
                    prev->link.next = &next->link;
                }
                else
                {
                    root = next;
                }
                */
            }
        }

        prev = insn;
    }

    return root;
}

static inline void
print_hex_bytes(const unsigned char *c, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i)
    {
        evil_print("%02X ", c[i]);
    }

    for (; i < 10; ++i)
    {
        evil_print("   ");
    }
}

static void
disassemble_procedure(struct environment_t *environment, struct object_t *args, const char *name)
{
    struct object_t *procedure;
    struct object_t *byte_code_object;
    const unsigned char *ptr;
    size_t i;
    size_t num_bytes;

    procedure = args;
    assert(procedure->tag_count.tag == TAG_PROCEDURE);

    byte_code_object = deref(&VECTOR_BASE(procedure)[FIELD_CODE]);
    assert(byte_code_object->tag_count.tag == TAG_STRING);

    ptr = (unsigned char *)byte_code_object->value.string_value;

    evil_print("%s:\n", name);

    for (i = 0, num_bytes = byte_code_object->tag_count.count; i < num_bytes;)
    {
        unsigned char c = ptr[i];

        evil_print("    %5d: ", i);

        switch (c)
        {
            case OPCODE_INVALID:
                print_hex_bytes(ptr + i, 1);
                evil_print("INVALID\n");
                ++i;
                break;
            case OPCODE_LDSLOT_X:
                {
                    union convert_two_t c2;
                    int index;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    index = c2.s2;
                    print_hex_bytes(ptr + i, 3);

                    evil_print("LDSLOT %d\n", index);
                }

                i += 3;
                break;
            case OPCODE_STSLOT_X:
                {
                    union convert_two_t c2;
                    int index;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    index = c2.s2;
                    print_hex_bytes(ptr + i, 3);

                    evil_print("STSLOT %d\n", index);
                }

                i += 3;
                break;
            case OPCODE_LDIMM_1_BOOL:
                {
                    unsigned char value;
                    
                    value = ptr[i + 1];
                    print_hex_bytes(ptr + i, 2);

                    evil_print("LDIMM_1_BOOL %s\n", value ? "#t" : "#f");
                }

                i += 2;
                break;
            case OPCODE_LDIMM_1_CHAR:
                {
                    unsigned char value;
                    
                    value = ptr[i + 1];
                    print_hex_bytes(ptr + i, 2);

                    evil_print("LDIMM_1_CHAR #\\%c\n", (char)value);
                }

                i += 2;
                break;
            case OPCODE_LDIMM_1_FIXNUM:
                {
                    unsigned char value;
                    
                    value = ptr[i + 1];
                    print_hex_bytes(ptr + i, 2);

                    evil_print("LDIMM_1_FIXNUM %d\n", (int)value);
                }

                i += 2;
                break;
            case OPCODE_LDIMM_1_FLONUM:
                {
                    unsigned char value;
                    
                    value = ptr[i + 1];
                    print_hex_bytes(ptr + i, 2);

                    evil_print("LDIMM_1_FLONUM %f\n", (double)value);
                }

                i += 2;
                break;
            case OPCODE_LDIMM_4_FIXNUM:
                {
                    union convert_four_t c4;

                    memcpy(c4.bytes, ptr + i + 1, 4);
                    print_hex_bytes(ptr + i, 5);

                    evil_print("LDIMM_4_FIXNUM %d\n", c4.s4);
                }

                i += 5;
                break;
            case OPCODE_LDIMM_4_FLONUM:
                {
                    union convert_four_t c4;

                    memcpy(c4.bytes, ptr + i + 1, 4);
                    print_hex_bytes(ptr + i, 5);

                    evil_print("LDIMM_4_FLONUM %f\n", c4.f4);
                }

                i += 5;
                break;
            case OPCODE_LDIMM_8_FIXNUM:
                {
                    union convert_eight_t c8;

                    memcpy(c8.bytes, ptr + i + 1, 8);
                    print_hex_bytes(ptr + i, 9);

                    evil_print("LDIMM_8_FIXNUM %" PRId64 "\n", c8.s8);
                }

                i += 9;
                break;
            case OPCODE_LDIMM_8_FLONUM:
                {
                    union convert_eight_t c8;

                    memcpy(c8.bytes, ptr + i + 1, 8);
                    print_hex_bytes(ptr + i, 9);

                    evil_print("LDIMM_8_FLONUM %lf\n", c8.f8);
                }

                i += 9;
                break;
            case OPCODE_LDIMM_8_SYMBOL:
                {
                    union convert_eight_t c8;

                    memcpy(c8.bytes, ptr + i + 1, 8);
                    print_hex_bytes(ptr + i, 9);

                    evil_print("LDIMM_8_SYMBOL %s\n", find_symbol_name(environment, c8.u8));
                }

                i += 9;
                break;
            case OPCODE_LDSTR:
                {
                    const char *str;
                    size_t length;

                    str = (const char *)ptr + 1;
                    length = strlen(str);
                    print_hex_bytes(ptr + i, length + 1);

                    evil_print("LDSTR %s\n", str);
                    i += length + 2;
                }

                break;
            case OPCODE_LDEMPTY:
                print_hex_bytes(ptr + i, 1);
                evil_print("LDEMPTY\n");
                ++i;
                break;
            case OPCODE_LDFN:
                print_hex_bytes(ptr + i, 1);
                evil_print("LDFN\n");
                ++i;
                break;
            case OPCODE_STORE:
                print_hex_bytes(ptr + i, 1);
                evil_print("STORE\n");
                ++i;
                break;
            case OPCODE_LOAD:
                print_hex_bytes(ptr + i, 1);
                evil_print("LOAD\n");
                ++i;
                break;
            case OPCODE_MAKE_REF:
                print_hex_bytes(ptr + i, 1);
                evil_print("MAKE_REF\n");
                ++i;
                break;
            case OPCODE_SET:
                print_hex_bytes(ptr + i, 1);
                evil_print("SET\n");
                ++i;
                break;
            case OPCODE_NEW:
                {
                    enum tag_t tag;

                    tag = (enum tag_t)ptr[i + 1];
                    print_hex_bytes(ptr + i, 2);

                    evil_print("NEW %s\n", type_name(tag));
                }

                i += 2;
                break;
            case OPCODE_NEW_VECTOR:
                print_hex_bytes(ptr + i, 1);
                evil_print("NEW_VECTOR\n");
                ++i;
                break;
            case OPCODE_CMP_EQUAL:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMP_EQUAL\n");
                ++i;
                break;
            case OPCODE_CMPN_EQ:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMPN_EQ\n");
                ++i;
                break;
            case OPCODE_CMPN_LT:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMPN_LT\n");
                ++i;
                break;
            case OPCODE_CMPN_GT:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMPN_GT\n");
                ++i;
                break;
            case OPCODE_CMPN_LE:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMPN_LE\n");
                ++i;
                break;
            case OPCODE_CMPN_GE:
                print_hex_bytes(ptr + i, 1);
                evil_print("CMPN_GE\n");
                ++i;
                break;
            case OPCODE_BRANCH:
                {
                    union convert_two_t c2;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    print_hex_bytes(ptr + i, 3);

                    evil_print("BRANCH %d\n", c2.s2 + i + 1);
                }

                i += 3;
                break;
            case OPCODE_COND_BRANCH:
                {
                    union convert_two_t c2;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    print_hex_bytes(ptr + i, 3);

                    evil_print("COND_BRANCH %d\n", c2.s2 + i + 3);
                }

                i += 3;
                break;
            case OPCODE_CALL:
                {
                    union convert_two_t c2;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    print_hex_bytes(ptr + i, 3);

                    evil_print("CALL %d\n", c2.s2);
                }

                i += 3;
                break;
            case OPCODE_TAILCALL:
                {
                    union convert_two_t c2;

                    memcpy(c2.bytes, ptr + i + 1, 2);
                    print_hex_bytes(ptr + i, 3);

                    evil_print("TAILCALL %d\n", c2.s2);
                }

                i += 3;
                break;
            case OPCODE_RETURN:
                print_hex_bytes(ptr + i, 1);
                evil_print("RETURN\n");
                ++i;
                break;
            case OPCODE_GET_BOUND_LOCATION:
                {
                    union convert_eight_t c8;

                    memcpy(c8.bytes, ptr + i + 1, 8);
                    print_hex_bytes(ptr + i, 9);

                    evil_print("GET_BOUND_LOCATION %s\n", find_symbol_name(environment, c8.u8));
                }

                i += 9;
                break;
            case OPCODE_ADD:
                print_hex_bytes(ptr + i, 1);
                evil_print("ADD\n");
                ++i;
                break;
            case OPCODE_SUB:
                print_hex_bytes(ptr + i, 1);
                evil_print("SUB\n");
                ++i;
                break;
            case OPCODE_MUL:
                print_hex_bytes(ptr + i, 1);
                evil_print("MUL\n");
                ++i;
                break;
            case OPCODE_DIV:
                print_hex_bytes(ptr + i, 1);
                evil_print("DIV\n");
                ++i;
                break;
            case OPCODE_AND:
                print_hex_bytes(ptr + i, 1);
                evil_print("AND\n");
                ++i;
                break;
            case OPCODE_OR:
                print_hex_bytes(ptr + i, 1);
                evil_print("OR\n");
                ++i;
                break;
            case OPCODE_XOR:
                print_hex_bytes(ptr + i, 1);
                evil_print("XOR\n");
                ++i;
                break;
            case OPCODE_NOT:
                print_hex_bytes(ptr + i, 1);
                evil_print("NOT\n");
                ++i;
                break;
            case OPCODE_NOP:
                print_hex_bytes(ptr + i, 1);
                evil_print("NOP\n");
                ++i;
                break;
            default:
                BREAK();
                break;
        }
    }
}

struct object_t
disassemble(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t symbol;
    struct object_t *slot;
    struct object_t *function;
    const char *name;

    UNUSED(num_args);
    assert(num_args == 1);
    symbol = *args;

    assert(symbol.tag_count.tag == TAG_SYMBOL);
    slot = get_bound_location(environment, symbol.value.symbol_hash, 1);
    assert(slot != NULL);

    function = deref(slot);
    assert(function != NULL);

    name = find_symbol_name(environment, symbol.value.symbol_hash);

    switch (function->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            evil_print("%s: <special function>\n", name);
            break;
        case TAG_PROCEDURE:
            disassemble_procedure(environment, function, name);
            break;
        default:
            BREAK();
            break;
    }

    return make_empty_ref();
}

static struct object_t
compile_form_to_bytecode(struct compiler_context_t *previous_context, struct environment_t *environment, struct object_t *lambda_body)
{
    struct object_t *args;
    struct object_t *body;
    struct object_t *procedure;
    struct instruction_t *root;
    struct compiler_context_t context;

    args = CAR(lambda_body);
    root = NULL;

    initialize_compiler_context(&context, environment, args, previous_context);

    for (body = CDR(lambda_body); body != empty_pair; body = CDR(body))
    {
        root = compile_form(&context, root, CAR(body));
    }

    root = add_return_insn(&context, root);
    collapse_nops(root);
    eliminate_branch_to_return(root);
    root = promote_tailcalls(root);

    procedure = assemble(environment, &context, root);

    destroy_compiler_context(&context);

    return make_ref(procedure);
}

static struct instruction_t *
compile_lambda(struct compiler_context_t *context, struct instruction_t *next, struct object_t *lambda_body)
{
    struct object_t procedure;
    struct environment_t *environment;

    /*
     * If this function is called then we're compiling a function that, when
     * called, generates a function. Just keep that in mind.
     */

    /*
     * Okay so here's what we need to do.
     *   1) Call lambda() to compile the lambda into a function object. When
     *      compiling lambda(), indicate that we want to check the parent
     *      scope(s) for values to capture.
     *   2) If we find a value in a parent scope to capture, re-compile the
     *      parent scope and turn that value into an environment global.
     *   3) Repeat until complete.
     *   n) when this function is called it needs to copy the function and
     *      copy the environment it links to. This will probably need new
     *      opcodes. (new closure?).
     *
     * Questions
     *   * It kinda makes sense to turn a function object into a structure
     *     that contains a pair of pointers, one to code, and one to its
     *     environment. Creating a new closure would create a new instance of
     *     this structure pointing to a cloned environment and the existing
     *     code.
     *   * When we create a new closure instance in code we need to track the
     *     code object somewhere. Do we bury a reference into the body of
     *     the code? Do we have some special function lookup table/registry?
     */

    environment = context->environment;

    procedure = compile_form_to_bytecode(context, environment, lambda_body);
    assert(procedure.tag_count.tag == TAG_REFERENCE);

    return compile_load_function_local(context, next, procedure.value.ref);
}

static struct instruction_t *
compile_load_function_local(struct compiler_context_t *context, struct instruction_t *next, struct object_t *object)
{
    int local_idx;
    struct function_local_t *function_local;
    struct instruction_t *load_this_fn;
    struct instruction_t *index;
    struct instruction_t *make_ref;
    struct instruction_t *load;

    function_local = pool_alloc(&context->pool, sizeof(struct function_local_t));
    function_local->object = object;
    function_local->link.next = &context->locals->link;
    context->locals = function_local;
    local_idx = context->num_fn_locals++;

    assert(local_idx <= (127 - FIELD_LOCALS));

    load_this_fn = allocate_instruction(context);
    load_this_fn->opcode = OPCODE_LDFN;
    load_this_fn->link.next = &next->link;

    index = allocate_instruction(context);
    index->opcode = OPCODE_LDIMM_1_FIXNUM;
    index->data.s1 = (char)(local_idx + FIELD_LOCALS);
    index->size = 1;
    index->link.next = &load_this_fn->link;

    make_ref = allocate_instruction(context);
    make_ref->opcode = OPCODE_MAKE_REF;
    make_ref->link.next = &index->link;

    load = allocate_instruction(context);
    load->opcode = OPCODE_LOAD;
    load->link.next = &make_ref->link;

    return load;
}

struct object_t
lambda(struct environment_t *environment, int num_args, struct object_t *lambda_body)
{
    struct object_t procedure;

    UNUSED(num_args);
    assert(num_args == 1);

    procedure = compile_form_to_bytecode(NULL, environment, lambda_body);
disassemble_procedure(environment, deref(&procedure), "unnamed procedure");

    return procedure;
    //return compile_form_to_bytecode(NULL, environment, lambda_body);
}

