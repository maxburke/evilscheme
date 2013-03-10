/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>

#include "base.h"
#include "test.h"

char *print_buffer;
int print_buffer_offset;
int print_buffer_size;

void
evil_print(const char *format, ...)
{
    va_list args;
    int required_length;
    int buffer_space;

    va_start(args, format);

    buffer_space = print_buffer_size - print_buffer_offset;
    required_length = vsnprintf(print_buffer + print_buffer_offset, buffer_space, format, args);

    if (required_length < buffer_space)
    {
        size_t size;
        const int reallocation_delta = 4096;

        size = (size_t)(print_buffer_size + reallocation_delta);
        print_buffer = realloc(print_buffer, size);
        memset(print_buffer, 0, size);
        print_buffer_size = size;

        vsnprintf(print_buffer + print_buffer_offset, print_buffer_size - print_buffer_offset, format, args);
    }
}

void
evil_reset_print_buffer(void)
{
    print_buffer_offset = 0;
}

int
evil_run_tests(void)
{

}
