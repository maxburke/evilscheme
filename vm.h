#ifndef VM_H
#define VM_H

/*
 * VM design:
 * Stack based in lieu of data registers. VM contains a condition register
 * that is used for the results of test conditions. 
 */

enum opcode_t
{
    OPCODE_INVALID,

    /*
     * OPCODE_LDARG_X [arg #] | -> [value]
     * Copy argument X (0 .. n-1) to the top of the stack
     */
    OPCODE_LDARG_X,

    /*
     * OPCODE_LDIMM_1_type [byte 0] | -> [value]
     * Create an object of the specified type on the top of the stack with its
     * value initialized to the byte parameter.
     */
    OPCODE_LDIMM_1_BOOL,
    OPCODE_LDIMM_1_CHAR,
    OPCODE_LDIMM_1_FIXNUM,
    OPCODE_LDIMM_1_FLONUM,

    /*
     * OPCODE_LDIMM_4_type [byte 0..3] | -> [value]
     * Like LDIMM_1 but provides four bytes for an initial value. This byte
     * value is in the endianness of the architecture the code is compiled
     * on. For flonum types, the value is the IEEE single precision floating
     * point representation of the number and will be expanded to double
     * when the opcode is executed. For fixnum types the value is sign
     * extended.
     */
    OPCODE_LDIMM_4_FIXNUM,
    OPCODE_LDIMM_4_FLONUM,

    /*
     * OPCODE_LDIMM_8_type [byte 0..7] | -> [value]
     * Same as LDIMM_4 but for 8 bytes. For flonum types the value is IEEE 
     * double precision floating point representation of the object, or 
     * as near as can be represented on the executing platform. For symbols,
     * the bytes represent the hashed value of the symbol (see object.c for
     * details on the hashing algorithm used.)
     */
    OPCODE_LDIMM_8_FIXNUM,
    OPCODE_LDIMM_8_FLONUM,
    OPCODE_LDIMM_8_SYMBOL,

    /*
     * OPCODE_LDSTR [string][0] | -> [reference]
     * Create a string object and push a reference of it to the top of the 
     * stack. The string that follows the opcode is null-terminated.
     */
    OPCODE_LDSTR,

    /*
     * OPCODE_MAKE_REF | [reference] [index] -> [slot reference]
     */
    OPCODE_MAKE_REF,

    /*
     * OPCODE_SET | [reference|value] [slot reference] -> 
     * Set the slot reference to the reference provided.
     */
    OPCODE_SET,

    /*
     * OPCODE_SET_CxR | [value|reference] [pair reference] ->
     * Set the car/cdr of the specified pair to the value/reference provided.
     */
    OPCODE_SET_CAR,
    OPCODE_SET_CDR,

    /*
     * OPCODE_NEW [type] | [reference]
     * NEW returns a new heap-allocated object.
     */
    OPCODE_NEW,

    /*
     * OPCODE_NEW_VECTOR | [size] -> [reference]
     * NEW_VECTOR creates a new heap-allocated vector of the specified size
     * and pushes a reference of it to the stack.
     */
    OPCODE_NEW_VECTOR,

    /*
     * OPCODE_CMP | [value/ref b] [value/ref a] -> 
     * Evaluates a CMP b and sets the appropriate condition flags.
     */
    OPCODE_CMP,

    /*
     * OPCODE_BRANCH_N [offset bytes 0 .. (N - 1)]
     * Sets the PC to (byteCodeBase + offsetValue).
     */
    OPCODE_BRANCH_1,
    OPCODE_BRANCH_2,

    /*
     * OPCODE_COND_BRANCH_N [condition] [offset bytes 0 .. (N - 1)]
     * Sets the PC to (byteCodeBase + offsetValue) if the specified condition
     * flags match the condition register.
     */
    OPCODE_COND_BRANCH_1,
    OPCODE_COND_BRANCH_2,

    /*
     * OPCODE_CALL 
     * Pops the target function from the stack. Pushes the return address, 
     * program area chain, and stack chain. Jumps to the specified method.
     */
    OPCODE_CALL,

    /*
     * OPCODE_TAILCALL
     * Pops the target function from the stack. Erases the invoking function
     * from the stack and then performs the function invocation as if it were
     * a call instruction.
     *
     * For example, in this case: (define foo (lambda (x) (bar x)))
     *
     * This could be compiled to the following bytecode:
     *    LDARG 0, LDIMM8_SYM <bar>, GET_BOUND_LOCATION, TAILCALL
     *
     * Before the call to bar, the stack would look like this:
     *    x | [foo's return] | [program area chain] | [stack chain] | x | <bar>
     *
     * Executing a normal CALL instruction would result in this stack layout:
     *    x | [foo's return] | [program area chain] | [stack chain] | x | [bar's return] | [program area chain] | [stack chain]
     * But a TAILCALL instruction would result in: 
     *    x | [foo's return] | [program area chain] | [stack chain] 
     */
    OPCODE_TAILCALL,

    /*
     * OPCODE_RETURN
     * Erases the current frame from the stack and jumps back to the saved 
     * return address.
     */
    OPCODE_RETURN,

    /*
     * OPCODE_GET_BOUND_LOCATION | [symbol] -> [slot reference | false]
     * Look up the symbol in the symbol table and push a slot reference to the
     * top of the stack if it exists or #f if it does not.
     */
    OPCODE_GET_BOUND_LOCATION,

    /*
     * OPCODE_arithmetic | [b] [a] -> [a OP b]
     * Perform arithmetic on the values on the top of the stack. The type of 
     * the expression [a OP b] is the less precise of the types of a and b.
     * For example, adding the fixnum 3 and flonum 1.0 gives the flonum 4.0.
     * This signals an error if the types are not fixnum or flonum.
     */
    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MUL,
    OPCODE_DIV,

    /*
     * OPCODE_DUP_X | [a] ... (x - 1) ... -> [a] ... (x - 1) ... [a]
     * Duplicates the stack element X items deep to the top of the stack.
     */
    OPCODE_DUP_X,

    /*
     * OPCODE_POP_X | [a] ... (x - 1) ... ->
     * Pops X items from the top of the stack.
     */
    OPCODE_POP_X,

    /*
     * OPCODE_SWAP_X | [a] ... (x - 1) ... [b] -> [b] ... (x - 1) ... [a]
     * Swaps the top of the stack with the value X items deep.
     */
    OPCODE_SWAP_X
};

enum condition_t
{
    CONDITION_EQ,
    CONDITION_LT,
    CONDITION_GT,
    CONDITION_NEQ,
    CONDITION_LE,
    CONDITION_GE,
    CONDITION_REFEQ
};

struct procedure_t
{
    struct tag_count_t tag_count;
    struct environment_t *environment;
    int num_args;
    unsigned char byte_code[1];
};

struct object_t *
vm_run(struct environment_t *environment, struct object_t *fn, struct object_t *args);

#endif

