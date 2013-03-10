/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "read.h"
#include "runtime.h"
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

static void
reset_print_buffer(void)
{
    print_buffer_offset = 0;
}

static char *
remove_character(char *buffer, const char * const end, char character)
{
    const char *read;
    char *write;

    /*
     * This function strips out any undesired  characters from the test input
     * as this may cause complications in comparing it to what is generated.
     */

    read = buffer;
    write = buffer;

    while (read < end)
    {
        const char c = *read;

        if (c == character)
        {
            ++read;
        }
        else
        {
            *write = c;
            ++write;
            ++read;
        }
    }

    *write = 0;
    assert(write <= end);

    return write;
}

static char * 
remove_comments(char *buffer, const char * const end)
{
    const char *read;
    char *write;

    read = buffer;
    write = buffer;

    while (read < end)
    {
        const char c = *read;

        if (c == ';')
        {
            while (*read != '\n')
                ++read;

            ++read;
        }
        else
        {
            *write = c;
            ++write;
            ++read;
        }
    }

    *write = 0;
    assert(write <= end);

    return write;
}

static char *
read_test_file(const char *filename)
{
    FILE *fp;
    size_t size;
    char *buffer;
    
    fp = fopen("rb", filename);
    fseek(fp, 0, SEEK_END);
    size = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buffer = calloc(size + 1, 1);
    fread(buffer, 1, size, fp);

    fclose(fp);

    return buffer;
}

static struct environment_t *
create_test_environment(void)
{
    size_t stack_size;
    size_t heap_size;
    void *stack;
    void *heap;

    stack_size = 1024 * sizeof(struct object_t);
    heap_size = 1024 * 1024;
    stack = malloc(stack_size);
    posix_memalign(&heap, 4096, heap_size);

    return evil_environment_create(stack, stack_size, heap, heap_size);
}

static int
run_test(struct environment_t *environment, const char *test, const char *expected)
{
    size_t test_length;
    struct object_t *args;
    struct object_t *input_object;
    struct object_t arg_ref;
    struct object_t object;
    struct object_t result;

    reset_print_buffer();

    test_length = strlen(test);
    args = gc_alloc(environment->heap, TAG_PAIR, 0);
    input_object = gc_alloc(environment->heap, TAG_STRING, test_length);
    memmove(input_object->value.string_value, test, test_length);

    *CAR(args) = make_ref(input_object);
    arg_ref = make_ref(args);
    object = read(environment, &arg_ref);
    result = eval(environment, &object);
    print(environment, &result);

    return strcmp(expected, print_buffer) != 0;
}

int
evil_run_tests(void)
{
    DIR *dir;
    struct dirent *entry;
    int result;
    struct environment_t *environment;

    #define TEST_DIR "tests"

    result = 0;
    environment = create_test_environment();
    dir = opendir(TEST_DIR);

    do
    {
        char filename[256];

        entry = readdir(dir);

        if (strstr(entry->d_name, ".test") != 0)
        {
            char *test_file;
            char *test_end;
            char *test;
            char *expected;

            snprintf(filename, sizeof(filename), TEST_DIR "/%s", entry->d_name);
            test_file = read_test_file(filename);
            test_end = test_file + strlen(test_file);

            test_end = remove_character(test_file, test_end, '\r');
            test_end = remove_comments(test_file, test_end);

            test = test_file;
            if (strstr(test_file, ">") == 0)
            {
                continue;
            }

            expected = strstr(test_file, ">") + 1;
            *expected = 0;
            ++expected;

            test_end = remove_character(expected, test_end, '>');

            result += run_test(environment, test, expected);

            free(test);
        }

    } while (entry != NULL);

    closedir(dir);

    return result;
}
