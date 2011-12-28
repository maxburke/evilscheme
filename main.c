#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "object.h"
#include "builtins.h"
#include "environment.h"
#include "read.h"

/*
 * main!
 */
int 
main(void) 
{
    const char *inputs[] = {
        "4",
        "(define count (lambda (item L) (if L (+ (equal? item (first L)) (count item (rest L))) 0)))",
        "(define fact (lambda (n) (if (<= n 1) 1 (* n (fact (- n 1))))))",
        "(define hello-world (lambda () (display \"hello world!\") (newline)))",
        "(define hello-world-2 (lambda (x) `(foo ,bar ,@(list 1 2 3))))",
        "(define vector-test #(1 2 3))"
    };
    size_t i;

    create_environment(&global_environment);
    initialize_global_environment(global_environment);

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i)
    {
        struct object_t *object;
        struct object_t *result;
        struct object_t *input_object;
        struct object_t *args;
        size_t input_length = strlen(inputs[i]);
    
        args = allocate_object(TAG_PAIR, 0);
        input_object = allocate_object(TAG_STRING, input_length);
        memmove(input_object->value.string_value, inputs[i], input_length);
        CAR(args) = input_object;

        object = read(global_environment, args);
        result = eval(global_environment, object);
        print(global_environment, result);
        printf("\n");
    }

    return 0;
}

