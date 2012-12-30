#include "object.h"

/*
 * Symbols are a value type that have their string representation stored
 * hashed. The hashing algorithm below is the FNV-1 hash algorithm.
 */

#define PRIME UINT64_C(1099511628211)

uint64_t
hash_string(const char *string)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    char c;

    while ((c = *string++))
    {
        hash = (hash * PRIME) ^ (uint64_t)(unsigned char)c;
    }

    return hash;
}

uint64_t
hash_bytes(const void *bytes, size_t num_bytes)
{
    const unsigned char *ptr = bytes;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;

    for (i = 0; i < num_bytes; ++i)
    {
        hash = (hash * PRIME) ^ (uint64_t)ptr[i];
    }

    return hash;
}

