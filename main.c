/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "object.h"
#include "evil_scheme.h"
#include "environment.h"
#include "gc.h"
#include "runtime.h"
#include "test.h"

#ifdef _MSC_VER
    #include <malloc.h>

    void *
    evil_aligned_alloc(size_t alignment, size_t size)
    {
        return _aligned_malloc(size, alignment);
    }

    void
    evil_aligned_free(void *ptr)
    {
        _aligned_free(ptr);
    }
#else
    void *
    evil_aligned_alloc(size_t alignment, size_t size)
    {
        void *mem;

        if (posix_memalign(&mem, alignment, size) != 0)
        {
            return NULL;
        }

        return mem;
    }

    void
    evil_aligned_free(void *ptr)
    {
        free(ptr);
    }
#endif

/*
 * TODO: add tests for closures:
 * (define closure-test (lambda (foo) (lambda () (set! foo (+ 1 foo)) foo)))
 * (define closure-test-fn (closure-test 0))
 * (closure-test-fn)
 * (closure-test-fn)
 */

int
main(int argc, char *argv[])
{
    return evil_run_tests(argc, argv);
}


