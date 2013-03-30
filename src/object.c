/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include "base.h"
#include "object.h"

const char *
type_name(enum evil_tag_t tag)
{
    switch (tag)
    {
        case TAG_INVALID:
            return "invalid";
        case TAG_BOOLEAN:
            return "boolean";
        case TAG_SYMBOL:
            return "symbol";
        case TAG_CHAR:
            return "char";
        case TAG_VECTOR:
            return "vector";
        case TAG_PAIR:
            return "pair";
        case TAG_FIXNUM:
            return "fixnum";
        case TAG_FLONUM:
            return "flonum";
        case TAG_STRING:
            return "string";
        case TAG_PROCEDURE:
            return "procedure";
        case TAG_SPECIAL_FUNCTION:
            return "special_function";
        case TAG_ENVIRONMENT:
            return "environment";
        case TAG_REFERENCE:
            return "reference";
        case TAG_INNER_REFERENCE:
            return "inner reference";
        default:
            BREAK();
            return "unknown";
    }
}

