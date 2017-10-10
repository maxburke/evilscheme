/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_LAMBDA_H
#define EVIL_LAMBDA_H

#include <stdint.h>

#include "slist.h"

struct instruction_t
{
    struct slist_t link;

    unsigned char opcode;
    int offset;
    size_t size;
    struct instruction_t *reloc;

    union data
    {
        unsigned char u1;
                 char s1;
        unsigned short u2;
                 short s2;
        unsigned int u4;
                 int s4;
        float f4;
        uint64_t u8;
        int64_t s8;
        double f8;
        char string[1];
    } data;
};

#endif

