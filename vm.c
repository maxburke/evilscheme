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
#   define VM_ASSERT_NOTRACE(x) if (!(x)) { fprintf(stderr, "%s:%d: Assertion failed: %s", __FILE__, __LINE__, #x); BREAK(); } else (void)0
#   define VM_ASSERT(x) if (!(x)) { vm_trace_stack(environment, sp, program_area); fprintf(stderr, "%s:%d: Assertion failed: %s", __FILE__, __LINE__, #x); BREAK(); } else (void)0
#else
#   define VM_ASSERT(x)
#endif

#define ENABLE_VM_TRACING 1

#if ENABLE_VM_TRACING
#   define VM_TRACE_OP(x) do { fprintf(stderr, "[vm] %32s program_area begin: %p sp begin: %p", #x, (void *)program_area, (void *)sp); } while (0)
#   define VM_TRACE(x) do { fprintf(stderr, "[vm] %s", x); } while (0)
#   define VM_CONTINUE() fprintf(stderr, " sp end: %p\n", (void *)sp); vm_trace_stack(environment, sp, program_area); continue
#else
#   define VM_TRACE_OP(x)
#   define VM_TRACE(x)
#   define VM_CONTINUE() continue
#endif

#define STACK_PUSH(stack, x) do { *(stack--) = x; } while (0)
#define STACK_POP(stack) *(++stack) 
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

static void
vm_push_args_to_stack(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *stack_ptr;

    stack_ptr = environment->stack_ptr;
    stack_ptr -= num_args;
    memmove(stack_ptr, args, num_args * sizeof(struct object_t));

    memset(environment->stack_bottom, 0, num_args * sizeof(struct object_t));

    environment->stack_ptr = stack_ptr - 1;
}

static inline struct object_t *
vm_push_return_address(struct object_t *sp, struct object_t *fn, unsigned short offset)
{
    struct object_t return_address;

    return_address.tag_count.tag = TAG_INNER_REFERENCE;
    return_address.tag_count.flag = 0;
    return_address.tag_count.count = offset;
    return_address.value.ref = fn;

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
    VM_ASSERT_NOTRACE(ref_type == TAG_REFERENCE || ref_type == TAG_INNER_REFERENCE);
#endif

    return referenced_object->tag_count.tag;
}

static inline void
vm_demote_numeric(struct object_t *object)
{
    VM_ASSERT_NOTRACE(object->tag_count.tag == TAG_FLONUM);

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

    VM_ASSERT_NOTRACE(a_tag != TAG_REFERENCE && b_tag != TAG_REFERENCE);

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
        case TAG_SPECIAL_FUNCTION:
            return a->value.special_function_value == b->value.special_function_value;
        case TAG_ENVIRONMENT:
            return 0;
        case TAG_HEAP:
            return 0;
        case TAG_PAIR:
            return vm_compare_equal(CAR(a), CAR(b)) && vm_compare_equal(CDR(a), CDR(b));
        case TAG_PROCEDURE:
            /*
             * Procedures are a special case of vector. They compare equal iff
             * all the fields are equal.
             */
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
            if (a->tag_count.count != b->tag_count.count)
                return 0;

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

union function_pointer_cast_t
{
    special_function_t special_function;
    void *pointer;
};

static void
vm_trace_stack(struct environment_t *environment, struct object_t *sp, struct object_t *program_area)
{
    struct object_t *stack_top;

    fprintf(stderr, "\n");

    for (stack_top = environment->stack_top - 1; stack_top >= sp; --stack_top)
    {
        if (stack_top == sp)
        {
            fprintf(stderr, "[vm] sp->\n");
            break;
        }
        else if (stack_top == program_area)
        {
            fprintf(stderr, "[vm] pa-> ");
        }
        else
        {
            fprintf(stderr, "[vm]      ");
        }

        fprintf(stderr, "0x%p ", (void *)stack_top);

        switch (stack_top->tag_count.tag)
        {
            case TAG_BOOLEAN:
                fprintf(stderr, "BOOLEAN          %s", stack_top->value.fixnum_value ? "#t" : "#f");
                break;
            case TAG_SYMBOL:
                fprintf(stderr, "SYMBOL           %" PRIx64 " %s", 
                        stack_top->value.symbol_hash, 
                        find_symbol_name(environment, stack_top->value.symbol_hash));
                break;
            case TAG_CHAR:
                fprintf(stderr, "CHAR             %c", (char)stack_top->value.fixnum_value);
                break;
            case TAG_FIXNUM:
                fprintf(stderr, "FIXNUM           %" PRId64, stack_top->value.fixnum_value);
                break;
            case TAG_FLONUM:
                fprintf(stderr, "FLONUM           %lf", stack_top->value.flonum_value);
                break;
            case TAG_REFERENCE:
                fprintf(stderr, "REFERENCE        0x%p", (void *)stack_top->value.ref);
                break;
            case TAG_INNER_REFERENCE:
                fprintf(stderr, "INNER REFERENCE  0x%p,%d", (void *)stack_top->value.ref, stack_top->tag_count.count);
                break;
            case TAG_SPECIAL_FUNCTION:
                {
                    union function_pointer_cast_t function_pointer_cast;

                    function_pointer_cast.special_function = stack_top->value.special_function_value;
                    fprintf(stderr, "SPECIAL FUNCTION 0x%p", function_pointer_cast.pointer);
                }
                break;
                break;
            case TAG_PROCEDURE:
            case TAG_PAIR:
            case TAG_VECTOR:
            case TAG_STRING:
                fprintf(stderr, "ERROR: REFERENCE TYPE ON STACK");
                break;
        }

        fprintf(stderr, "\n");
    }
}

static inline unsigned char *
vm_extract_code_pointer(struct object_t *procedure)
{
    struct object_t *byte_code;

    byte_code = deref(&VECTOR_BASE(procedure)[FIELD_CODE]);
    assert(byte_code->tag_count.tag == TAG_STRING);

    return (unsigned char *)byte_code->value.string_value;
}

static inline int
vm_extract_num_args(struct object_t *procedure)
{
    struct object_t *num_args;

    num_args = &VECTOR_BASE(procedure)[FIELD_NUM_ARGS];
    assert(num_args->tag_count.tag == TAG_FIXNUM);

    return (int)num_args->value.fixnum_value;
}

static inline int
vm_extract_num_locals(struct object_t *procedure)
{
    struct object_t *num_locals;

    num_locals = &VECTOR_BASE(procedure)[FIELD_NUM_LOCALS];
    assert(num_locals->tag_count.tag == TAG_FIXNUM);

    return (int)num_locals->value.fixnum_value;
}

struct object_t
vm_run(struct environment_t *environment, struct object_t *initial_function, int num_args, struct object_t *args)
{
    struct object_t *procedure;
    struct object_t *program_area;
    struct object_t *sp;
    struct object_t *old_stack;
    unsigned char *pc_base;
    unsigned char *pc;

    /*
     * This is going to get really ugly if the GC moves our procedure object
     * while we are executing something. Maybe we should pin the procedures
     * while we are executing? Refresh after every function call and/or 
     * object creation?
     */

    procedure = deref(initial_function);
    assert(procedure->tag_count.tag == TAG_PROCEDURE);

    old_stack = environment->stack_ptr;
    sp = old_stack;
    pc = vm_extract_code_pointer(procedure);
    pc_base = pc;

    /*
     * Stack layout for VM:
     * [high addresses]...............................................................[low addresses]
     * [arg n - 1][...][arg 1][arg 0][program area chain][return address][stack top]...[stack_bottom]
     *                                      ^                                 ^
     *                  program_area -------+                           ------+
     *
     * arg0 is at program_area[1]
     */

    vm_push_args_to_stack(environment, num_args, args);
    assert(num_args == vm_extract_num_args(procedure));
    sp = environment->stack_ptr;

    /*
     * The stack pointer points to the first free stack slot, so to point
     * correctly at the program area the stack pointer must be incremented.
     */
    program_area = sp + 1;

    sp = vm_push_ref(sp, NULL);                 /* program area chain */  
    sp = vm_push_return_address(sp, NULL, 0);   /* return address */
    sp -= vm_extract_num_locals(procedure);
   
    for (;;)
    {
        unsigned byte = *pc++;

        switch (byte)
        {
            case OPCODE_INVALID:
                VM_TRACE_OP(OPCODE_INVALID);
                BREAK();
                VM_CONTINUE();
            case OPCODE_LDSLOT_X:
                VM_TRACE_OP(OPCODE_LDSLOT_X);
                {
                    union convert_two_t c2;
                    short offset;

                    c2.bytes[0] = *pc++;
                    c2.bytes[1] = *pc++;

                    offset = c2.s2;
                    STACK_PUSH(sp, program_area[offset]);
                }
                VM_CONTINUE();
            case OPCODE_STSLOT_X:
                VM_TRACE_OP(OPCODE_STSLOT_X);
                {
                    union convert_two_t c2;
                    short offset;

                    c2.bytes[0] = *pc++;
                    c2.bytes[1] = *pc++;

                    offset = c2.s2;
                    program_area[offset] = STACK_POP(sp);
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
            case OPCODE_LDFN:
                VM_TRACE_OP(OPCODE_LDFN);
                sp = vm_push_ref(sp, procedure);
                VM_CONTINUE();
            case OPCODE_LOAD:
                VM_TRACE_OP(OPCODE_LOAD);
                {
                    struct object_t *ref;
                    struct object_t *object;
                    unsigned char tag;

                    ref = sp + 1;

                    tag = ref->tag_count.tag;
                    VM_ASSERT(tag == TAG_REFERENCE || tag == TAG_INNER_REFERENCE);

                    if (tag == TAG_INNER_REFERENCE)
                    {
                        unsigned char object_tag;

                        object = ref->value.ref;
                        object_tag = object->tag_count.tag;
                        
                        if (object_tag == TAG_VECTOR || object_tag == TAG_PROCEDURE || object_tag == TAG_SPECIAL_FUNCTION)
                        {
                            unsigned short index;

                            index = ref->tag_count.count;
                            *ref = VECTOR_BASE(object)[index];
                        }
                        else
                        {
                            BREAK();
                        }
                    }
                    else
                    {
                        object = ref;
                        *ref = *deref(object);
                    }
                }
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
                    union convert_eight_t c8;
                    struct object_t *object;

                    memcpy(c8.bytes, pc, 8);
                    pc += 8;

                    object = get_bound_location(environment, c8.u8, 1);
                    sp = vm_push_ref(sp, object);
                }
                VM_CONTINUE();
            case OPCODE_CALL:
                VM_TRACE_OP(OPCODE_CALL);
                {
                    struct object_t *old_program_area;
                    struct object_t *fn;
                    unsigned char tag;
                    union convert_two_t c2;

                    memcpy(c2.bytes, pc, 2);
                    pc += 2;

                    fn = deref(sp + 1);
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
                         * Save the return address and create space for the local slots.
                         */
                        sp = vm_push_ref(sp, old_program_area);
                        sp = vm_push_return_address(sp, fn, (unsigned short)return_offset);
                        sp -= vm_extract_num_locals(fn);

                        assert((int)c2.u2 == vm_extract_num_args(fn));

                        /*
                         * call the function!
                         */
                        pc = vm_extract_code_pointer(fn);
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
                    struct object_t *fn;
                    struct object_t *arg_slot;
                    unsigned char tag;
                    int current_fn_num_args;
                    int num_args;
                    int arg_diff;
                    union convert_two_t c2;
                    struct object_t *prev_program_area_ref;
                    struct object_t *return_address;
                    struct object_t *moved_prev_program_area_ref;
                    struct object_t *moved_return_address;

                    memcpy(c2.bytes, pc, 2);
                    pc += 2;

                    fn = deref(sp + 1);
                    ++sp;
                    tag = fn->tag_count.tag;

                    num_args = vm_extract_num_args(fn);
                    assert(num_args == (int)c2.u2);
                    /*
                     * This code erases the current frame replacing it with the new call.
                     * At this point the stack, pre-call, where function b is performing
                     * a tail call to function a, looks like this:
                     * [arg a0][arg a1]...[arg an]...[stuff]...[return][PA chain][arg b0][arg b1]...[arg bk]
                     * And we need to move the args a0-an on top of args b0-bk. The 
                     * great thing is the return slot and the PA chain don't need to
                     * change, they can just remain the same.
                     */

                    /*
                     * First, save the program area chain and the return
                     * address to the bottom of the stack as we may clobber
                     * these values when we juggle the arguments below.
                     */
                    prev_program_area_ref = program_area - 1;
                    return_address = program_area - 2;

                    *(sp) = *prev_program_area_ref;
                    *(sp - 1) = *return_address;

                    moved_prev_program_area_ref = sp;
                    moved_return_address = sp - 1;

                    /*
                     * Second, calculate where we need to displace the program
                     * area in order to land our new arguments.
                     */
                    current_fn_num_args = vm_extract_num_args(procedure);
                    arg_diff = current_fn_num_args - num_args;
                    arg_slot = program_area + arg_diff;

                    /*
                     * Move our arguments.
                     */
                    memmove(arg_slot, sp + 1, num_args * sizeof(struct object_t));

                    /*
                     * Adjust the stack pointer down below the program area and
                     * below where the locals go.
                     */
                    sp = arg_slot - 3;
                    sp -= vm_extract_num_locals(procedure);

                    /*
                     * Set the new program area.
                     */
                    program_area = arg_slot;

                    /*
                     * Restore the saved program area chain and return address.
                     */
                    *(program_area - 1) = *moved_prev_program_area_ref;
                    *(program_area - 2) = *moved_return_address;

                    if (tag == TAG_SPECIAL_FUNCTION)
                    {
                        struct object_t *procedure_base;
                        struct object_t result;
                        special_function_t function_pointer;

                        procedure_base = VECTOR_BASE(fn);
                        function_pointer = procedure_base[FIELD_CODE].value.special_function_value;

                        result = function_pointer(environment, num_args, program_area);
                        sp += num_args;
                        *(sp + 1) = result;
                    }
                    else
                    {
                        pc = vm_extract_code_pointer(fn);
                        pc_base = pc;
                        procedure = fn;
                    }
                }
                VM_CONTINUE();
            case OPCODE_RETURN:
                VM_TRACE_OP(OPCODE_RETURN);
                {
                    struct object_t *return_address;
                    struct object_t *prev_program_area_ref;
                    struct object_t *return_value;
                    struct object_t *parent;

                    return_value = sp + 1;
                    return_address = program_area - 2;
                    prev_program_area_ref = program_area - 1;
                    VM_ASSERT(return_address->tag_count.tag == TAG_INNER_REFERENCE);
                    VM_ASSERT(prev_program_area_ref->tag_count.tag == TAG_REFERENCE);
                    parent = return_address->value.ref;

                    if (parent != NULL)
                    {
                        pc = vm_extract_code_pointer(parent) + return_address->tag_count.count;
                        pc_base = pc;
                        procedure = parent;

                        sp = program_area + vm_extract_num_args(procedure) - 2;
                        *(sp + 1) = *return_value;

                        program_area = deref(prev_program_area_ref);
                    }
                    else
                    {
                        sp = program_area + vm_extract_num_args(procedure) - 2;
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

