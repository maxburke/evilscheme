#include "object.h"

#define PRIME UINT64_C(1099511628211)

uint64_t
hash_string(const char *string)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    char c;

    while (c = *string++)
    {
        hash = (hash * PRIME) ^ (uint64_t)(unsigned char)c;
    }

    return hash;
}

uint64_t
hash_bytes(const char *bytes, size_t num_bytes)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;

    for (i = 0; i < num_bytes; ++i)
    {
        hash = (hash * PRIME) ^ (uint64_t)(unsigned char)bytes[i];
    }

    return hash;
}

