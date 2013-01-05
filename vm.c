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
    #ifdef NDEBUG
        #define ENABLE_VM_ASSERTS 1
    #else
        #define ENABLE_VM_ASSERTS 0
    #endif
#endif

#if ENABLE_VM_ASSERTS
    #define VM_ASSERT(x) if (!(x)) { fprintf(stderr, "%s:%d: Assertion failed: %s", __FILE__, __LINE__, #x); BREAK(); } else (void)0
#else
    #define VM_ASSERT(x)
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

union four_byte_union_t
{
    unsigned char bytes[4];
    int fixnum_value;
    float flonum_value;
};

#define LDIMM_4_IMPL(TAG, FIELD) {                                                          \
        union four_byte_union_t fb;                                                         \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        sp->value.FIELD = fb.FIELD;                                                         \
        --sp;                                                                               \
    }

#define LDIMM_4_FIXNUM() LDIMM_4_IMPL(TAG_FIXNUM, fixnum_value)
#define LDIMM_4_FLONUM() LDIMM_4_IMPL(TAG_FLONUM, flonum_value)

union eight_byte_union_t
{
    unsigned char bytes[8];
    int64_t fixnum_value;
    double flonum_value;
    uint64_t symbol_hash;
};

#define LDIMM_8_IMPL(TAG, FIELD) {                                                          \
        union eight_byte_union_t fb;                                                        \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        fb.bytes[4] = *pc++; fb.bytes[5] = *pc++; fb.bytes[6] = *pc++; fb.bytes[7] = *pc++; \
        sp->value.FIELD = fb.FIELD;                                                         \
        --sp;                                                                               \
    }

#define LDIMM_8_FIXNUM() LDIMM_8_IMPL(TAG_FIXNUM, fixnum_value)
#define LDIMM_8_FLONUM() LDIMM_8_IMPL(TAG_FLONUM, flonum_value)
#define LDIMM_8_SYMBOL() LDIMM_8_IMPL(TAG_SYMBOL, symbol_hash)

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
        struct object_t *a = sp + 1;                                                        \
        struct object_t *b = sp + 2;                                                        \
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

#define FIXNUM_BINOP(OP) {\
        struct object_t *a = sp + 1;                                                        \
        struct object_t *b = sp + 2;                                                        \
                                                                                            \
        ENSURE_NUMERIC(a->tag_count.tag);                                                   \
        ENSURE_NUMERIC(b->tag_count.tag);                                                   \
        VM_ASSERT(a_tag == b_tag);                                                          \
        b->value.fixnum_value = a->value.fixnum_value OP b->value.fixnum_value;             \
        --sp;                                                                               \
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

    for (i = args; CAR(i) != empty_pair; i = CDR(i))
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
vm_push_ref(struct object_t *sp, struct object_t *object)
{
    struct object_t return_address;

    return_address.tag_count.tag = TAG_REFERENCE;
    return_address.tag_count.flag = 0;
    return_address.tag_count.count = 1;
    return_address.value.ref.object = object;

#if ENABLE_VM_ASSERTS
    return_address.value.ref.index = 0;
#endif

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

    inner_reference.tag_count.tag = TAG_INNER_REFERENCE;
    inner_reference.tag_count.flag = 0;
    inner_reference.tag_count.count = 1;
    inner_reference.value.ref.object = object;
    inner_reference.value.ref.index = index;

    return inner_reference;
}

static inline unsigned char
vm_reference_type(struct object_t *ref)
{
    const struct object_t *referenced_object = ref->value.ref.object;

#if ENABLE_VM_ASSERTS
    const unsigned char ref_type = ref->tag_count.tag;
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

static inline const struct object_t *
vm_flatten_reference(const struct object_t *object)
{
    VM_ASSERT(object->tag_count.tag != TAG_INNER_REFERENCE);

    if (object->tag_count.tag != TAG_REFERENCE)
        return object;

    return object->value.ref.object;
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
            return a->value.pair[0] == b->value.pair[0] 
                && a->value.pair[1] == b->value.pair[1];
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
            return vm_compare_equal_reference_type(
                    vm_flatten_reference(a), 
                    vm_flatten_reference(b));
        case TAG_INNER_REFERENCE:
            if (b_tag == TAG_INNER_REFERENCE)
            {
                return a->value.ref.object == b->value.ref.object
                    && a->value.ref.index == b->value.ref.index;
            }
            return 0;
        default:
            BREAK();
    }

    return 0;
}

struct object_t *
vm_run(struct environment_t *environment, struct object_t *fn, struct object_t *args)
{
    struct procedure_t *procedure;
    struct object_t *program_area;
    struct object_t *sp;
    struct object_t *old_stack;
    int num_args;
    unsigned char *pc;
   
    procedure = (struct procedure_t *)fn;
    old_stack = environment->stack_ptr;
    sp = old_stack;
    program_area = old_stack;
    pc = procedure->byte_code;

    /*
     * Stack layout for VM:
     * [high addresses]............................................................................[low addresses]
     * [arg n - 1][...][arg 1][arg 0][return address][program area chain][stack chain][stack top]...[stack_bottom]
     *                                      ^                                               ^
     *                  program_area -------+                                     sp -------+
     *
     * arg0 is at program_area[1]
     */

    num_args = vm_push_args_to_stack(environment, args);
    assert(num_args == procedure->num_args);
    sp = vm_push_ref(sp, NULL);    /* return address */
    sp = vm_push_ref(sp, NULL);    /* program area chain */
    sp = vm_push_ref(sp, NULL);    /* stack chain */
    sp = environment->stack_ptr;
    fn = environment->stack_ptr;
    
    for (;;)
    {
        unsigned byte = *pc++;

        switch (byte)
        {
            case OPCODE_INVALID:
                BREAK();
                break;
            case OPCODE_LDARG_X:
                {
                    unsigned char arg_index = *pc++;
                    STACK_PUSH(sp, program_area[arg_index]);
                }
                break;
            case OPCODE_LDIMM_1_BOOL:
                LDIMM_1_BOOLEAN()
                break;
            case OPCODE_LDIMM_1_CHAR:
                LDIMM_1_CHAR()
                break;
            case OPCODE_LDIMM_1_FIXNUM:
                LDIMM_1_FIXNUM()
                break;
            case OPCODE_LDIMM_1_FLONUM:
                LDIMM_1_FLONUM()
                break;
            case OPCODE_LDIMM_4_FIXNUM:
                LDIMM_4_FIXNUM()
                break;
            case OPCODE_LDIMM_4_FLONUM:
                LDIMM_4_FLONUM()
                break;
            case OPCODE_LDIMM_8_FIXNUM:
                LDIMM_8_FIXNUM()
                break;
            case OPCODE_LDIMM_8_FLONUM:
                LDIMM_8_FLONUM()
                break;
            case OPCODE_LDIMM_8_SYMBOL:
                LDIMM_8_SYMBOL()
                break;
            case OPCODE_LDSTR:
                {
                    struct object_t *string_obj;
                    size_t string_length;

                    string_length = strlen((const char *)pc);
                    string_obj = gc_alloc(environment->heap, TAG_STRING, string_length);
                    strcpy(string_obj->value.string_value, (const char *)pc);
                    sp = vm_push_ref(sp, string_obj);

                    pc += string_length + 1;
                }
                break;
            case OPCODE_MAKE_REF:
                {
                    struct object_t * const ref = sp + 2;
                    struct object_t * const index = sp + 1;
                    VM_ASSERT(ref->tag_count.tag == TAG_REFERENCE);
                    VM_ASSERT(index->tag_count.tag == TAG_FIXNUM);

                    *ref = vm_create_inner_reference(ref->value.ref.object, index->value.fixnum_value);
                    ++sp;
                }
                break;
            case OPCODE_SET:
                {
                    struct object_t *source = sp + 2;
                    struct object_t *ref = sp + 1;
                    struct object_t *ref_obj = ref->value.ref.object;
                    int64_t ref_index = ref->value.ref.index;

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
                break;

            case OPCODE_SET_CAR:
            case OPCODE_SET_CDR:
            case OPCODE_NEW:
            case OPCODE_NEW_VECTOR:
                BREAK();
                break;
            case OPCODE_CMP_EQUAL:
                {
                    struct object_t *b = sp + 2;
                    struct object_t *a = sp + 1;
                    int is_equal;
                    
                    is_equal = vm_compare_equal(a, b);
                    sp = vm_push_bool(sp + 2, is_equal);
                }
                break;
            case OPCODE_CMPN_EQ:
                CMPN_IMPL(==)
                break;
            case OPCODE_CMPN_LT:
                CMPN_IMPL(<)
                break;
            case OPCODE_CMPN_GT:
                CMPN_IMPL(>)
                break;
            case OPCODE_CMPN_LE:
                CMPN_IMPL(<=)
                break;
            case OPCODE_CMPN_GE:
                CMPN_IMPL(>=)
                break;
            case OPCODE_BRANCH_1:
                {
                    int offset = (int)((char)*pc);
                    pc += offset + 1;
                }
                break;
            case OPCODE_BRANCH_2:
                {
                    int offset = (int)(*(short *)pc);
                    pc += offset + 2;
                }
                break;
            case OPCODE_COND_BRANCH_1:
                {
                    struct object_t *condition;
                    int offset;

                    condition = sp + 1;
                    VM_ASSERT(condition->tag_count.tag == TAG_BOOLEAN);
                    offset = (int)((char)*pc);
                    ++pc;

                    if (condition->value.fixnum_value)
                    {
                        pc += offset;
                    }

                    ++sp;
                }
                break;
            case OPCODE_COND_BRANCH_2:
                {
                    struct object_t *condition;
                    int offset;

                    condition = sp + 1;
                    VM_ASSERT(condition->tag_count.tag == TAG_BOOLEAN);
                    offset = (int)(*(short *)pc);
                    pc += 2;

                    if (condition->value.fixnum_value)
                    {
                        pc += offset;
                    }

                    ++sp;
                }
                break;
            case OPCODE_GET_BOUND_LOCATION:
            case OPCODE_CALL:
            case OPCODE_TAILCALL:
                BREAK();
                break;

            case OPCODE_RETURN:
                BREAK();
                goto vm_execution_done;

            case OPCODE_ADD:
                FIXNUM_BINOP(+)
                break;
            case OPCODE_SUB:
                FIXNUM_BINOP(-)
                break;
            case OPCODE_MUL:
                FIXNUM_BINOP(*)
                break;
            case OPCODE_DIV:
                FIXNUM_BINOP(/)
                break;
            case OPCODE_AND:
                FIXNUM_BINOP(&)
                break;
            case OPCODE_OR:
                FIXNUM_BINOP(|)
                break;
            case OPCODE_XOR:
                FIXNUM_BINOP(^)
                break;
            case OPCODE_NOT:
            case OPCODE_DUP_X:
            case OPCODE_POP_X:
            case OPCODE_SWAP_X:
            default:
                BREAK();
                break;
        }
    }

vm_execution_done:
    environment->stack_ptr = old_stack;

    /*
     * This should probably cons the last return value on the stack and
     * return that instead.
     */
    return NULL;
}

