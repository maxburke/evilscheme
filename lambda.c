#include "base.h"
#include "builtins.h"
#include "object.h"

struct object_t *
lambda(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    UNUSED(args);

    print(environment, args);

    BREAK(); 
    return NULL;
}


