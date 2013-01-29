/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

DISABLE_WARNING(4127)
DISABLE_WARNING(4996)

/*
 * Stack etiquette:
 * The stack pointer points to the first available slot in the stack (ie: use first, then decrement).
 */

#ifndef ENABLE_VM_ASSERTS
#   ifndef NDEBUG
#       define ENABLE_VM_ASSERTS 1
#   else
#       define ENABLE_VM_ASSERTS 0
#   endif
#endif

#if ENABLE_VM_ASSERTS
#   define VM_ASSERT(x) if (!(x)) { fprintf(stderr, "%s:%d: Assertion failed: %s", __FILE__, __LINE__, #x); BREAK(); } else (void)0
#else
#   define VM_ASSERT(x)
#endif

#define ENABLE_VM_TRACING 0

#if ENABLE_VM_TRACING
#   define VM_TRACE_OP(x) do { fprintf(stderr, "[vm] %32s program_area begin: %p sp begin: %p", #x, program_area, sp); } while (0)
#   define VM_TRACE(x) do { fprintf(stderr, "[vm] %s", x); } while (0)
#   define VM_CONTINUE(); fprintf(stderr, " sp end: %p\n", sp); continue
#else
#   define VM_TRACE_OP(x)
#   define VM_TRACE(x)
#   define VM_CONTINUE();   continue
#endif

#define STACK_PUSH(stack, x) do { *(stack--) = x; } while (0)
#define STACK_POP(stack) *(stack++) 
#ifndef NDEBUG
#   define VALIDATE_STACK(env) do { \
        assert(env->stack_ptr >= env->stack_bottom && env->stack_ptr < env->stack_top); } while(0)
#else
#   define VALIDATE_STACK(env)
#endif

static inline int
vm_compare_equal(const struct object_t *a, const struct object_t *b);

#define LDIMM_1_IMPL(FIELD, TYPE, TAG) {        \
        sp->tag_count.tag = TAG;                \
        sp->tag_count.flag = 0;                 \
        sp->tag_count.count = 1;                \
        sp->value.FIELD = (TYPE)(*pc++);        \
        --sp;                                   \
    }

#define LDIMM_1_BOOLEAN()   LDIMM_1_IMPL(fixnum_value, int64_t, TAG_BOOLEAN)
#define LDIMM_1_CHAR()      LDIMM_1_IMPL(fixnum_value, int64_t, TAG_CHAR)
#define LDIMM_1_FIXNUM()    LDIMM_1_IMPL(fixnum_value, int64_t, TAG_FIXNUM)
#define LDIMM_1_FLONUM()    LDIMM_1_IMPL(flonum_value, double, TAG_FLONUM)

#define LDIMM_4_IMPL(TAG, FIELD, UNION_FIELD) {                                             \
        union convert_four_t fb;                                                            \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        sp->value.FIELD = fb.UNION_FIELD;                                                   \
        --sp;                                                                               \
    }

#define LDIMM_4_FIXNUM() LDIMM_4_IMPL(TAG_FIXNUM, fixnum_value, s4)
#define LDIMM_4_FLONUM() LDIMM_4_IMPL(TAG_FLONUM, flonum_value, f4)

#define LDIMM_8_IMPL(TAG, FIELD, UNION_FIELD) {                                             \
        union convert_eight_t fb;                                                           \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        fb.bytes[4] = *pc++; fb.bytes[5] = *pc++; fb.bytes[6] = *pc++; fb.bytes[7] = *pc++; \
        sp->value.FIELD = fb.UNION_FIELD;                                                   \
        --sp;                                                                               \
    }

#define LDIMM_8_FIXNUM() LDIMM_8_IMPL(TAG_FIXNUM, fixnum_value, s8)
#define LDIMM_8_FLONUM() LDIMM_8_IMPL(TAG_FLONUM, flonum_value, f8)
#define LDIMM_8_SYMBOL() LDIMM_8_IMPL(TAG_SYMBOL, symbol_hash, u8)

#if ENABLE_VM_ASSERTS
    #define ENSURE_NUMERIC(X) VM_ASSERT(X == TAG_FIXNUM || X == TAG_FLONUM) 
#else
    #define ENSURE_NUMERIC(X)
#endif

#define CONDITIONAL_DEMOTE(A, B) do {                                                       \
        if (a_tag == TAG_FIXNUM && b_tag == TAG_FLONUM)                                     \
        {                                                                                   \
            vm_demote_numeric(A);                                                           \
        }                                                                                   \
        else if (a_tag == TAG_FLONUM && b_tag == TAG_FIXNUM)                                \
        {                                                                                   \
            vm_demote_numeric(B);                                                           \
        }                                                                                   \
    } while (0)

#define CMPN_IMPL(OP) {                                                                     \
        struct object_t *a = deref(sp + 1);                                                 \
        struct object_t *b = deref(sp + 2);                                                 \
        unsigned char a_tag = a->tag_count.tag;                                             \
        unsigned char b_tag = b->tag_count.tag;                                             \
        int result;                                                                         \
                                                                                            \
        ENSURE_NUMERIC(a_tag);                                                              \
        ENSURE_NUMERIC(b_tag);                                                              \
        CONDITIONAL_DEMOTE(a, b);                                                           \
        result = (a_tag == TAG_FIXNUM)                                                      \
            ? (a->value.fixnum_value OP b->value.fixnum_value)                              \
            : (a->value.flonum_value OP b->value.flonum_value);                             \
        sp = vm_push_bool(sp + 2, result);                                                  \
    }

#define NUMERIC_BINOP(OP) {                                                                 \
        struct object_t *a = deref(sp + 2);                                                 \
        struct object_t *b = deref(sp + 1);                                                 \
        unsigned char a_tag = a->tag_count.tag;                                             \
        unsigned char b_tag = b->tag_count.tag;                                             \
                                                                                            \
        ENSURE_NUMERIC(a->tag_count.tag);                                                   \
        ENSURE_NUMERIC(b->tag_count.tag);                                                   \
        CONDITIONAL_DEMOTE(a, b);                                                           \
        if (a_tag == TAG_FIXNUM)                                                            \
        {                                                                                   \
            a->value.fixnum_value = a->value.fixnum_value OP b->value.fixnum_value;         \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            a->value.flonum_value = a->value.flonum_value OP b->value.flonum_value;         \
        }                                                                                   \
        ++sp;                                                                               \
    }

#define FIXNUM_BINOP(OP) {                                                                  \
        struct object_t *a = deref(sp + 2);                                                 \
        struct object_t *b = deref(sp + 1);                                                 \
        unsigned char a_tag = a->tag_count.tag;                                             \
        unsigned char b_tag = b->tag_count.tag;                                             \
        UNUSED(a_tag);                                                                      \
        UNUSED(b_tag);                                                                      \
                                                                                            \
        ENSURE_NUMERIC(a->tag_count.tag);                                                   \
        ENSURE_NUMERIC(b->tag_count.tag);                                                   \
        VM_ASSERT(a_tag == b_tag);                                                          \
        a->value.fixnum_value = a->value.fixnum_value OP b->value.fixnum_value;             \
        ++sp;                                                                               \
    }

static int
vm_push_args_to_stack(struct environment_t *environment, struct object_t *args)
{
    struct object_t *i;
    struct object_t *stack_ptr;
    struct object_t *ptr;
    int count;

    /*
     * vm_push_args_to_stack pushes the args in forward order to the bottom of
     * the stack and then memcpy's them to where they need to be. This allows
     * them to be in the correct order without having to do some complex
     * push + reversal.
     */
    
    stack_ptr = environment->stack_ptr;
    ptr = environment->stack_bottom;
    count = 0;

    for (i = args; i != empty_pair; i = CDR(i))
    {
        struct object_t *object;
        
        object = CAR(i);
        assert(object->tag_count.count == 1);
        assert(ptr < stack_ptr);
        *ptr++ = *object;
        ++count;
    }

    stack_ptr -= count;

    memmove(stack_ptr,
            environment->stack_bottom,
            count * sizeof(struct object_t));

    memset(environment->stack_bottom,
            0,
            count * sizeof(struct object_t));

    environment->stack_ptr = stack_ptr - 1;

    return count;
}

static inline struct object_t *
vm_push_return_address(struct object_t *sp, struct procedure_t *object, unsigned short offset)
{
    struct object_t return_address;

    return_address.tag_count.tag = TAG_INNER_REFERENCE;
    return_address.tag_count.flag = 0;
    return_address.tag_count.count = offset;
    return_address.value.ref = (struct object_t *)object;

    *(sp--) = return_address;

    return sp;
}

static inline struct object_t *
vm_push_ref(struct object_t *sp, struct object_t *object)
{
    struct object_t return_address;

    return_address.tag_count.tag = TAG_REFERENCE;
    return_address.tag_count.flag = 0;
    return_address.tag_count.count = 1;
    return_address.value.ref = object;

    *(sp--) = return_address;

    return sp;
}

static inline struct object_t *
vm_push_bool(struct object_t *sp, int val)
{
    struct object_t boolean;

    boolean.tag_count.tag = TAG_BOOLEAN;
    boolean.tag_count.flag = 0;
    boolean.tag_count.count = 1;
    boolean.value.fixnum_value = val;

    *(sp--) = boolean;

    return sp;
}

static inline struct object_t
vm_create_inner_reference(struct object_t *object, int64_t index)
{
    struct object_t inner_reference;

    assert(index >= 0 && index < 65536);

    inner_reference.tag_count.tag = TAG_INNER_REFERENCE;
    inner_reference.tag_count.flag = 0;
    inner_reference.tag_count.count = (unsigned short)index;
    inner_reference.value.ref = object;

    return inner_reference;
}

static inline unsigned char
vm_reference_type(struct object_t *ref)
{
    const struct object_t *referenced_object;
#if ENABLE_VM_ASSERTS
    const unsigned char ref_type = ref->tag_count.tag;
#endif
    
    referenced_object = ref->value.ref;

#if ENABLE_VM_ASSERTS
    VM_ASSERT(ref_type == TAG_REFERENCE || ref_type == TAG_INNER_REFERENCE);
#endif

    return referenced_object->tag_count.tag;
}

static inline void
vm_demote_numeric(struct object_t *object)
{
    VM_ASSERT(object->tag_count.tag == TAG_FLONUM);

    object->tag_count.tag = TAG_FLONUM;
    object->value.flonum_value = (double)(object->value.fixnum_value);
}

static inline struct object_t *
vm_vector_index(struct object_t *vector, int64_t index)
{
    struct tag_count_t *header;
    void *vector_element_base;
    
    header = &vector->tag_count;
    vector_element_base = header + 1;

    return (struct object_t *)vector_element_base + index;
}

static inline int
vm_compare_equal_reference_type(const struct object_t *a, const struct object_t *b)
{
    const unsigned char a_tag = a->tag_count.tag;
    const unsigned char b_tag = b->tag_count.tag;

    VM_ASSERT(a_tag != TAG_REFERENCE && b_tag != TAG_REFERENCE);

    /*
     * If the references point to the same object, they are equal.
     */
    if (a == b)
        return 1;

    /*
     * If the objects are of different types, they are NOT equal.
     */
    if (a_tag != b_tag)
        return 0;

    /*
     * Equal objects should have an equal count field. For non-aggregates
     * this should be 1 and for aggregates this should be the number of
     * contained elements (byte code bytes, vector elements, etc.)
     */
    if (a->tag_count.count != b->tag_count.count)
        return 0;

    switch (a_tag)
    {
        case TAG_PROCEDURE:
            {
                const struct procedure_t *proc_a = (const struct procedure_t *)a;
                const struct procedure_t *proc_b = (const struct procedure_t *)b;

                return memcmp(proc_a->byte_code, proc_b->byte_code, a->tag_count.count) == 0;
            }
        case TAG_SPECIAL_FUNCTION:
            return a->value.special_function_value == b->value.special_function_value;
        case TAG_ENVIRONMENT:
            return 0;
        case TAG_HEAP:
            return 0;
        case TAG_PAIR:
            return vm_compare_equal(CAR(a), CAR(b)) && vm_compare_equal(CDR(a), CDR(b));
        case TAG_VECTOR:
            {
                unsigned short i, e;

                for (i = 0, e = a->tag_count.count; i != e; ++i)
                {
                    if (!vm_compare_equal(
                                vm_vector_index((struct object_t *)a, i),
                                vm_vector_index((struct object_t *)b, i)))
                        return 0;
                }

                return 1;
            }
        case TAG_STRING:
            return memcmp(a->value.string_value, b->value.string_value, a->tag_count.count) == 0;
        default:
            BREAK();
            break;
    }

    return 0;
}

static inline int
vm_compare_equal(const struct object_t *a, const struct object_t *b)
{
    const unsigned char a_tag = a->tag_count.tag;
    const unsigned char b_tag = b->tag_count.tag;

    if (a_tag != b_tag)
        return 0;

    switch (a_tag)
    {
        case TAG_BOOLEAN:
        case TAG_CHAR:
        case TAG_FIXNUM:
            return a->value.fixnum_value == b->value.fixnum_value;
        case TAG_FLONUM:
            return a->value.flonum_value == b->value.flonum_value;
        case TAG_SYMBOL:
            return a->value.symbol_hash == b->value.symbol_hash;
        case TAG_REFERENCE:
            return vm_compare_equal_reference_type(const_deref(a), const_deref(b));
        case TAG_INNER_REFERENCE:
            if (b_tag == TAG_INNER_REFERENCE)
            {
                return a->value.ref == b->value.ref
                    && a->tag_count.count == b->tag_count.count;
            }
            return 0;
        default:
            BREAK();
    }

    return 0;
}

struct object_t
vm_run(struct environment_t *environment, struct object_t *fn, struct object_t *args)
{
    struct procedure_t *procedure;
    struct object_t *program_area;
    struct object_t *sp;
    struct object_t *old_stack;
    int num_args;
    unsigned char *pc_base;
    unsigned char *pc;
   
    procedure = (struct procedure_t *)fn;
    old_stack = environment->stack_ptr;
    sp = old_stack;
    pc = procedure->byte_code;
    pc_base = procedure->byte_code;

    /*
     * Stack layout for VM:
     * [high addresses]...............................................................[low addresses]
     * [arg n - 1][...][arg 1][arg 0][program area chain][return address][stack top]...[stack_bottom]
     *                                      ^                                 ^
     *                  program_area -------+                           ------+
     *
     * arg0 is at program_area[1]
     */

    num_args = vm_push_args_to_stack(environment, args);
    assert(num_args == procedure->num_args);
    sp = environment->stack_ptr;

    /*
     * The stack pointer points to the first free stack slot, so to point
     * correctly at the program area the stack pointer must be incremented.
     */
    program_area = sp + 1;

    sp = vm_push_ref(sp, NULL);                 /* program area chain */  
    sp = vm_push_return_address(sp, NULL, 0);   /* return address */
    
    for (;;)
    {
        unsigned byte = *pc++;

        switch (byte)
        {
            case OPCODE_INVALID:
                VM_TRACE_OP(OPCODE_INVALID);
                BREAK();
                VM_CONTINUE();
            case OPCODE_LDARG_X:
                VM_TRACE_OP(OPCODE_LDARG_X);
                {
                    unsigned char arg_index = *pc++;
                    STACK_PUSH(sp, program_area[arg_index]);
                }
                VM_CONTINUE();
            case OPCODE_LDIMM_1_BOOL:
                VM_TRACE_OP(OPCODE_LDIMM_1_BOOL);
                LDIMM_1_BOOLEAN()
                VM_CONTINUE();
            case OPCODE_LDIMM_1_CHAR:
                VM_TRACE_OP(OPCODE_LDIMM_1_CHAR);
                LDIMM_1_CHAR()
                VM_CONTINUE();
            case OPCODE_LDIMM_1_FIXNUM:
                VM_TRACE_OP(OPCODE_LDIMM_1_FIXNUM);
                LDIMM_1_FIXNUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_1_FLONUM:
                VM_TRACE_OP(OPCODE_LDIMM_1_FLONUM);
                LDIMM_1_FLONUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_4_FIXNUM:
                VM_TRACE_OP(OPCODE_LDIMM_4_FIXNUM);
                LDIMM_4_FIXNUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_4_FLONUM:
                VM_TRACE_OP(OPCODE_LDIMM_4_FLONUM);
                LDIMM_4_FLONUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_8_FIXNUM:
                VM_TRACE_OP(OPCODE_LDIMM_8_FIXNUM);
                LDIMM_8_FIXNUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_8_FLONUM:
                VM_TRACE_OP(OPCODE_LDIMM_8_FLONUM);
                LDIMM_8_FLONUM()
                VM_CONTINUE();
            case OPCODE_LDIMM_8_SYMBOL:
                VM_TRACE_OP(OPCODE_LDIMM_8_SYMBOL);
                LDIMM_8_SYMBOL()
                VM_CONTINUE();
            case OPCODE_LDSTR:
                VM_TRACE_OP(OPCODE_LDSTR);
                {
                    struct object_t *string_obj;
                    size_t string_length;

                    string_length = strlen((const char *)pc);
                    string_obj = gc_alloc(environment->heap, TAG_STRING, string_length);
                    strcpy(string_obj->value.string_value, (const char *)pc);
                    sp = vm_push_ref(sp, string_obj);

                    pc += string_length + 1;
                }
                VM_CONTINUE();
            case OPCODE_LDEMPTY:
                VM_TRACE_OP(OPCODE_LDEMPTY);
                sp = vm_push_ref(sp, empty_pair);
                VM_CONTINUE();
            case OPCODE_LOAD:
                VM_TRACE_OP(OPCODE_LOAD);
                BREAK();
                VM_CONTINUE();
            case OPCODE_MAKE_REF:
                VM_TRACE_OP(OPCODE_MAKE_REF);
                {
                    struct object_t * const ref = sp + 2;
                    struct object_t * const index = sp + 1;
                    VM_ASSERT(ref->tag_count.tag == TAG_REFERENCE);
                    VM_ASSERT(index->tag_count.tag == TAG_FIXNUM);

                    *ref = vm_create_inner_reference(ref->value.ref, index->value.fixnum_value);
                    ++sp;
                }
                VM_CONTINUE();
            case OPCODE_SET:
                VM_TRACE_OP(OPCODE_SET);
                {
                    struct object_t *source = sp + 2;
                    struct object_t *ref = sp + 1;
                    struct object_t *ref_obj = ref->value.ref;
                    unsigned short ref_index = ref->tag_count.count;

                    const unsigned char target_type = ref_obj->tag_count.tag; 

                    VM_ASSERT(target_type == TAG_STRING || target_type == TAG_VECTOR);

                    if (target_type == TAG_STRING)
                    {
                        char *target;
                        char source_value;
                        
                        VM_ASSERT(source->tag_count.tag == TAG_CHAR);
                        target = &ref_obj->value.string_value[ref_index];
                        source_value = (char)source->value.fixnum_value;

                        *target = source_value;
                    }
                    else
                    {
                        struct object_t *target;

                        target = vm_vector_index(ref_obj, ref_index);
                        *target = *source;
                    }

                    sp += 2;
                }
                VM_CONTINUE();
            case OPCODE_SET_CAR:
                VM_TRACE_OP(OPCODE_SET_CAR);
                BREAK();
                VM_CONTINUE();
            case OPCODE_SET_CDR:
                VM_TRACE_OP(OPCODE_SET_CDR);
                BREAK();
                VM_CONTINUE();
            case OPCODE_NEW:
                VM_TRACE_OP(OPCODE_NEW);
                BREAK();
                VM_CONTINUE();
            case OPCODE_NEW_VECTOR:
                VM_TRACE_OP(OPCODE_NEW_VECTOR);
                BREAK();
                VM_CONTINUE();
            case OPCODE_CMP_EQUAL:
                VM_TRACE_OP(OPCODE_CMP_EQUAL);
                {
                    struct object_t *b = sp + 2;
                    struct object_t *a = sp + 1;
                    int is_equal;
                    
                    is_equal = vm_compare_equal(a, b);
                    sp = vm_push_bool(sp + 2, is_equal);
                }
                VM_CONTINUE();
            case OPCODE_CMPN_EQ:
                VM_TRACE_OP(OPCODE_CMPN_EQ);
                CMPN_IMPL(==)
                VM_CONTINUE();
            case OPCODE_CMPN_LT:
                VM_TRACE_OP(OPCODE_CMPN_LT);
                CMPN_IMPL(<)
                VM_CONTINUE();
            case OPCODE_CMPN_GT:
                VM_TRACE_OP(OPCODE_CMPN_GT);
                CMPN_IMPL(>)
                VM_CONTINUE();
            case OPCODE_CMPN_LE:
                VM_TRACE_OP(OPCODE_CMPN_LE);
                CMPN_IMPL(<=)
                VM_CONTINUE();
            case OPCODE_CMPN_GE:
                VM_TRACE_OP(OPCODE_CMPN_GE);
                CMPN_IMPL(>=)
                VM_CONTINUE();
            case OPCODE_BRANCH:
                VM_TRACE_OP(OPCODE_BRANCH);
                {
                    union convert_two_t c2;

                    c2.bytes[0] = *pc++;
                    c2.bytes[1] = *pc++;

                    pc += c2.s2;
                }
                VM_CONTINUE();
            case OPCODE_COND_BRANCH:
                VM_TRACE_OP(OPCODE_COND_BRANCH);
                {
                    union convert_two_t c2;
                    struct object_t *condition;
                    int offset;

                    condition = sp + 1;
                    VM_ASSERT(condition->tag_count.tag == TAG_BOOLEAN);
                    c2.bytes[0] = *pc++;
                    c2.bytes[1] = *pc++;
                    offset = c2.s2;

                    if (condition->tag_count.tag != TAG_BOOLEAN || condition->value.fixnum_value != 0)
                    {
                        pc += offset;
                    }

                    ++sp;
                }
                VM_CONTINUE();
            case OPCODE_GET_BOUND_LOCATION:
                VM_TRACE_OP(OPCODE_GET_BOUND_LOCATION);
                {
                    struct object_t *symbol;
                    struct object_t *object;
                    uint64_t symbol_hash;

                    symbol = sp + 1;
                    VM_ASSERT(symbol->tag_count.tag == TAG_SYMBOL);
                    symbol_hash = symbol->value.symbol_hash;

                    object = get_bound_location(environment, symbol_hash, 1);
                    *symbol = *object;
                }
                VM_CONTINUE();
            case OPCODE_CALL:
                VM_TRACE_OP(OPCODE_CALL);
                {
                    struct object_t *old_program_area;
                    struct procedure_t *fn;
                    unsigned char tag;

                    fn = (struct procedure_t *)deref(sp + 1);
                    ++sp;
                    tag = fn->tag_count.tag;

                    VM_ASSERT(tag == TAG_PROCEDURE || tag == TAG_SPECIAL_FUNCTION);

                    if (tag == TAG_PROCEDURE)
                    {
                        ptrdiff_t return_offset;

                        return_offset = pc - pc_base;
                        VM_ASSERT(return_offset >= 0 && return_offset < fn->tag_count.count);
                        old_program_area = program_area;
                        program_area = sp + 1;

                        /*
                         * Save the return address.
                         */
                        sp = vm_push_ref(sp, old_program_area);
                        sp = vm_push_return_address(sp, fn, (unsigned short)return_offset);

                        /*
                         * call the function!
                         */
                        pc = fn->byte_code;
                        pc_base = pc;
                        procedure = fn;
                    }
                    else
                    {
                        /*
                         * TODO: This needs to take the arguments from the 
                         * stack and combine them into a list. This is probably
                         * going to be really ugly.
                         */
                        BREAK();
                    }
                }
                VM_CONTINUE();
            case OPCODE_TAILCALL:
                VM_TRACE_OP(OPCODE_TAILCALL);
                {
                    struct procedure_t *fn;
                    struct object_t *arg_slot;
                    unsigned char tag;
                    int current_fn_num_args;
                    int num_args;
                    int arg_diff;

                    fn = (struct procedure_t *)deref(sp + 1);
                    ++sp;
                    tag = fn->tag_count.tag;

                    if (tag == TAG_SPECIAL_FUNCTION)
                    {
                        /*
                         * Tail calls to C functions are not currently 
                         * supported.
                         */

                        BREAK();
                    }

                    /*
                     * This code erases the current frame replacing it with the new call.
                     * At this point the stack, pre-call, where function b is performing
                     * a tail call to function a, looks like this:
                     * [arg a0][arg a1]...[arg an]...[stuff]...[return][PA chain][arg b0][arg b1]...[arg bk]
                     * And we need to move the args a0-an on top of args b0-bk. The 
                     * great thing is the return slot and the PA chain don't need to
                     * change, they can just remain the same.
                     */

                    current_fn_num_args = procedure->num_args;
                    num_args = fn->num_args;
                    arg_diff = current_fn_num_args - num_args;

                    arg_slot = program_area + arg_diff;
                    memmove(arg_slot - 2, program_area - 2, 2 * sizeof(struct object_t));
                    memmove(arg_slot, sp + 1, num_args * sizeof(struct object_t));

                    pc = fn->byte_code;
                    pc_base = pc;
                    procedure = fn;
                    sp = arg_slot - 3;
                    program_area = arg_slot;
                }
                VM_CONTINUE();
            case OPCODE_RETURN:
                VM_TRACE_OP(OPCODE_RETURN);
                {
                    struct object_t *return_address;
                    struct object_t *prev_program_area_ref;
                    struct object_t *return_value;
                    struct procedure_t *parent;

                    return_value = sp + 1;
                    return_address = program_area - 2;
                    prev_program_area_ref = program_area - 1;
                    VM_ASSERT(return_address->tag_count.tag == TAG_INNER_REFERENCE);
                    VM_ASSERT(prev_program_area_ref->tag_count.tag == TAG_REFERENCE);
                    parent = (struct procedure_t *)return_address->value.ref;

                    if (parent != NULL)
                    {
                        pc = parent->byte_code + return_address->tag_count.count;
                        pc_base = pc;
                        procedure = parent;

                        sp = program_area + procedure->num_args - 2;
                        *(sp + 1) = *return_value;

                        program_area = deref(prev_program_area_ref);
                    }
                    else
                    {
                        sp = program_area + procedure->num_args - 2;
                        *(sp + 1) = *return_value;

                        goto vm_execution_done;
                    }
                }
                VM_CONTINUE();
            case OPCODE_ADD:
                VM_TRACE_OP(OPCODE_ADD);
                NUMERIC_BINOP(+)
                VM_CONTINUE();
            case OPCODE_SUB:
                VM_TRACE_OP(OPCODE_SUB);
                NUMERIC_BINOP(-)
                VM_CONTINUE();
            case OPCODE_MUL:
                VM_TRACE_OP(OPCODE_MUL);
                NUMERIC_BINOP(*)
                VM_CONTINUE();
            case OPCODE_DIV:
                VM_TRACE_OP(OPCODE_DIV);
                NUMERIC_BINOP(/)
                VM_CONTINUE();
            case OPCODE_AND:
                VM_TRACE_OP(OPCODE_AND);
                FIXNUM_BINOP(&)
                VM_CONTINUE();
            case OPCODE_OR:
                VM_TRACE_OP(OPCODE_OR);
                FIXNUM_BINOP(|)
                VM_CONTINUE();
            case OPCODE_XOR:
                VM_TRACE_OP(OPCODE_XOR);
                FIXNUM_BINOP(^)
                VM_CONTINUE();
            case OPCODE_NOT:
                VM_TRACE_OP(OPCODE_NOT);
                BREAK();
                VM_CONTINUE();
            case OPCODE_DUP_X:
                VM_TRACE_OP(OPCODE_DUP_X);
                BREAK();
                VM_CONTINUE();
            case OPCODE_POP_X:
                VM_TRACE_OP(OPCODE_POP_X);
                BREAK();
                VM_CONTINUE();
            case OPCODE_SWAP_X:
                VM_TRACE_OP(OPCODE_SWAP_X);
                BREAK();
                VM_CONTINUE();
            case OPCODE_NOP:
                VM_TRACE_OP(OPCODE_NOP);
                VM_CONTINUE();
            default:
                BREAK();
                VM_CONTINUE();
        }
    }

vm_execution_done:
    environment->stack_ptr = old_stack;

    /*
     * This should probably cons the last return value on the stack and
     * return that instead.
     */
    return *(sp + 1);
}

