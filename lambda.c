#include <stdio.h>

#include "base.h"
#include "builtins.h"
#include "object.h"
#include "vm.h"

struct object_t *
lambda(struct environment_t *environment, struct object_t *args)
{
    struct object_t *i;

    UNUSED(environment);

    for (i = args; CAR(i) != empty_pair; i = CDR(i))
    {
        print(environment, i);
    }

    BREAK(); 
    return NULL;
}


