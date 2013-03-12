/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifdef NDEBUG
#undef NDEBUG
#endif

#define _CRT_SECURE_NO_WARNINGS

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
    #define WIN32_LEAN_AND_MEAN
    #pragma warning(push, 0)
    #include <Windows.h>
    #pragma warning(pop)
    #define snprintf _snprintf
#else
    #include <dirent.h>
#endif

#include "base.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "read.h"
#include "runtime.h"
#include "test.h"
#include "user.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

char *print_buffer;
int print_buffer_offset;
int print_buffer_size;

void
evil_debug_print(const char *format, ...)
{
    /*
     * Swallow all debug output while the tests are exsecuting.
     */

    UNUSED(format);
}

void
evil_print(const char *format, ...)
{
    /*
     * This overloads the evil_print function with our own hook to record all
     * data written via evil_print.
     */
    va_list args;
    int required_length;
    int buffer_space;

    va_start(args, format);

    buffer_space = print_buffer_size - print_buffer_offset;
    required_length = vsnprintf(print_buffer + print_buffer_offset, buffer_space, format, args);

    if (buffer_space < required_length)
    {
        size_t size;
        const int reallocation_delta = 4096;

        size = (size_t)(print_buffer_size + reallocation_delta);
        print_buffer = realloc(print_buffer, size);
        memset(print_buffer + print_buffer_size, 0, reallocation_delta);
        print_buffer_size = size;

        vsnprintf(print_buffer + print_buffer_offset, print_buffer_size - print_buffer_offset, format, args);
    }

    print_buffer_offset += required_length;
}

static void
reset_print_buffer(void)
{
    print_buffer_offset = 0;
}

static void
free_print_buffer(void)
{
    free(print_buffer);
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
    /*
     * Comments in the test code are represented like scheme comments,
     * that is they start with a semicolon and go to the end of the line.
     */
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
    
    fp = fopen(filename, "rb");
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
    heap = evil_aligned_alloc(4096, heap_size);

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

    return strcmp(expected, print_buffer) == 0;
}

#ifdef _MSC_VER
    typedef HANDLE directory_t;
    static WIN32_FIND_DATA file_data;
    static char directory_root[260];
    int traversal_done;

    static directory_t
    begin_directory_traversal(const char *directory)
    {
        directory_t dir;
        char directory_buffer[260];

        GetCurrentDirectory(sizeof directory_root, directory_root);
        strncat(directory_root, "\\", sizeof directory_root);
        strncat(directory_root, directory, sizeof directory_root);

        strncpy(directory_buffer, directory_root, sizeof directory_buffer);
        strncat(directory_buffer, "\\*.test", sizeof directory_buffer);

        dir = FindFirstFile(directory_buffer, &file_data);

        if (dir == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                printf("Could not find test files.\n");
            }

            if (GetLastError() == ERROR_PATH_NOT_FOUND)
            {
                printf("Could not find test directory.\n");
            }
        }

        return dir;
    }

    static int
    is_valid_directory(directory_t dir)
    {
        return dir != INVALID_HANDLE_VALUE;
    }

    static int
    get_next_file(directory_t dir, char *name_buffer, size_t name_buffer_length)
    {
        BOOL result;

        if (traversal_done)
        {
            return 1;
        }

        strncpy(name_buffer, directory_root, name_buffer_length);
        strncat(name_buffer, "\\", name_buffer_length);
        strncat(name_buffer, file_data.cFileName, name_buffer_length);

        name_buffer[name_buffer_length - 1] = 0;

        result = FindNextFile(dir, &file_data);

        if (result == FALSE)
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
            {
                printf("Unknown error in FindNextFile: %d\n", GetLastError());
                return 1;
            }

            traversal_done = 1;
        }

        return 0; 
    }

    static void
    end_directory_traversal(directory_t dir)
    {
        FindClose(dir);
    }
#else
    typedef DIR *directory_t;
    static char directory_root[256];

    static directory_t
    begin_directory_traversal(const char *directory)
    {
        strncpy(directory_root, directory, sizeof directory_root);

        return opendir(directory);
    }

    static int
    is_valid_directory(directory_t dir)
    {
        return dir != NULL;
    }

    static int
    get_next_file(directory_t dir, char *name_buffer, size_t name_buffer_length)
    {
        struct dirent *entry;

        for (;;)
        {
            entry = readdir(dir);

            if (entry == NULL)
            {
                return 1;
            }

            if (strstr(entry->d_name, ".test") != 0)
            {
                snprintf(name_buffer, name_buffer_length, "%s/%s", directory_root, entry->d_name);
                return 0;
            }
        }
    }

    static void
    end_directory_traversal(directory_t dir)
    {
        closedir(dir);
    }
#endif

static void
dump_buffer_as_hex(const void *buffer, size_t length)
{
    /*
     * This prints a hex dump of a buffer to stdout with the offsets,
     * hex bytes, and the char representation.
     */
    size_t i;
    const unsigned char *ptr;

    ptr = buffer;

    for (i = 0; i < length; i += 16)
    {
        size_t num;
        size_t ii;

        num = MIN(length - i, 16);

        printf("%04x: ", (unsigned int)i);

        for (ii = 0; ii < num; ++ii)
        {
            printf("%02x ", ptr[i + ii]);
        }

        for (; ii < 16; ++ii)
        {
            printf("   ");
        }

        for (ii = 0; ii < num; ++ii)
        {
            char c;

            c = isprint((char)ptr[i + ii]) ? (char)ptr[i + ii] : '.';

            printf("%c", c);
        }

        printf("\n");
    }
}

static const char *
print_prefixed_line(const char *str, char c)
{
    /*
     * This function is a helper for the unified diff method below and prints a
     * line of text prefixed, either with a ' ' for no diff, '+' for lines added,
     * and '-' for lines removed.
     */
    printf("%c", c);

    for (;;)
    {
        char c = *str++;

        if (c == '\0')
        {
            return NULL;
        }

        printf("%c", c);

        if (c == '\n')
        {
            break;
        }
    }

    return str;
}

static void
diff_strings(const char *a, const char *b)
{
    /*
     * Sometimes it's difficult to see the difference between the actual and
     * expected output from just hex dumps/raw strings, so this prints out a
     * unified diff of the two strings.
     */
    const char *newline_a;
    const char *newline_b;

    for (;;)
    {
        size_t a_line_length;
        size_t b_line_length;

        newline_a = strchr(a, '\n');
        newline_b = strchr(b, '\n');

        if (newline_a == NULL && newline_b == NULL)
        {
            if (strcmp(a, b) != 0)
            {
                print_prefixed_line(a, '-');
                print_prefixed_line(b, '+');
            }
            else
            {
                print_prefixed_line(a, ' ');
            }

            return;
        }
        else if (newline_a == NULL)
        {
            const char *ptr;

            print_prefixed_line(a, '-');
            printf("\n");

            ptr = b;
            while ((ptr = print_prefixed_line(ptr, '+')) != NULL)
                ;

            return;
        }
        else if (newline_b == NULL)
        {
            const char *ptr;

            ptr = a;
            while ((ptr = print_prefixed_line(ptr, '-')) != NULL)
                ;

            printf("\n");
            print_prefixed_line(b, '+');

            return;
        }

        a_line_length = newline_a - a;
        b_line_length = newline_b - b;

        if ((a_line_length != b_line_length) || (memcmp(a, b, a_line_length) != 0))
        {
            print_prefixed_line(a, '-');
            print_prefixed_line(b, '+');
            a = newline_a + 1;
            b = newline_b + 1;
            continue;
        }

        print_prefixed_line(a, ' ');
        a = newline_a + 1;
        b = newline_b + 1;
        continue;
    }
}

static void
report_test_result(const char *test_name, int result, const char *expected)
{
    if (result == 0)
    {
        /*
         * If a test fails this routine prints out what we expected the
         * output to look like, what it actually was, hex dumps of both,
         * and a unified diff of the output to make it easier to debug.
         */
        printf("%s FAILED\n", test_name);
        printf("Expected:\n");
        printf("----------------------------------------\n");
        printf("%s\n", expected);
        dump_buffer_as_hex(expected, strlen(expected));

        printf("\nActual:\n");
        printf("----------------------------------------\n");
        printf("%s\n", print_buffer);
        dump_buffer_as_hex(print_buffer, strlen(print_buffer));

        printf("\nDiffs:\n");
        printf("----------------------------------------\n");
        diff_strings(expected, print_buffer);
    }
    else
    {
        printf("%s PASSED\n", test_name);
    }
}

int
evil_run_tests(void)
{
    directory_t dir;
    int result;
    struct environment_t *environment;
    int num_tests;
    int num_passed;

    #define TEST_DIR "tests"

    result = 0;
    num_tests = 0;
    num_passed = 0;
    environment = create_test_environment();
    dir = begin_directory_traversal(TEST_DIR);

    if (!is_valid_directory(dir))
    {
        return 1;
    }

    for (;;)
    {
        char filename[260];
        char *test_file;
        char *test_end;
        char *test;
        char *expected;
        int result;

        if (get_next_file(dir, filename, sizeof filename))
        {
            break;
        }

        test_file = read_test_file(filename);
        test_end = test_file + strlen(test_file);

        test_end = remove_character(test_file, test_end, '\r');
        test_end = remove_comments(test_file, test_end);

        test = test_file;
      
        expected = test_file;

        /*
         * Expected output is listed in the test file as lines starting with
         * the > character. The loop is to differentiate, say, (if (> foo bar) ...)
         * from proper test structure, like this:
         * (display "hello world")
         * >"hello world"
         */
        for (;;)
        {
            expected = strchr(expected, '>');

            if (expected == NULL)
            {
                goto next_test;
            }

            if (*(expected - 1) == '\n')
            {
                break;
            }
            
            ++expected;
        }

        *expected = 0;
        ++expected;

        result = run_test(environment, test, expected);
        report_test_result(filename, result, expected);

        ++num_tests;
        num_passed += result;

    next_test:
        free(test);
    }

    end_directory_traversal(dir);
    free_print_buffer();

    printf("\n========================================\n");
    printf("%d/%d tests passed\n", num_passed, num_tests);

    return num_tests - num_passed;
}