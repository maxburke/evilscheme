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
#include <float.h>
#include <math.h>
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
    #include <time.h>
#endif

#include "base.h"
#include "environment.h"
#include "evil_scheme.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "test.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define NUM_BENCHMARK_ITERATIONS 30

char *print_buffer;
size_t print_buffer_offset;
size_t print_buffer_size;

struct test_t;

static void
print_unit_test_summary(struct test_t *tests, int num_tests);

static void
print_benchmark_summary(struct test_t *tests, int num_tests);

static void
(*print_test_summary)(struct test_t *tests, int num_tests) = print_unit_test_summary;

void
evil_debug_printf(const char *format, ...)
{
    UNUSED(format);
}

void
evil_printf(const char *format, ...)
{
    /*
     * This overloads the evil_print function with our own hook to record all
     * data written via evil_print.
     */
    va_list args;
    int required_length;
    size_t buffer_space;

    va_start(args, format);

    assert(print_buffer_size >= print_buffer_offset);
    buffer_space = print_buffer_size - print_buffer_offset;
    required_length = vsnprintf(print_buffer + print_buffer_offset, buffer_space, format, args);

    if (buffer_space < (size_t)required_length)
    {
        size_t size;
        const int reallocation_delta = 4096;

        size = (size_t)(print_buffer_size + reallocation_delta);
        print_buffer = realloc(print_buffer, size);
        memset(print_buffer + print_buffer_size, 0, reallocation_delta);
        print_buffer_size = size;

        va_end(args);
        va_start(args, format);
        vsnprintf(print_buffer + print_buffer_offset, print_buffer_size - print_buffer_offset, format, args);
    }

    print_buffer_offset += required_length;

    va_end(args);
    va_start(args, format);
    vprintf(format, args);
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
    size_t bytes_read;
    char *buffer;

    fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    size = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buffer = calloc(size + 1, 1);
    bytes_read = fread(buffer, 1, size, fp);
    assert(bytes_read == size);

    fclose(fp);

    return buffer;
}

void *stack;
void *heap;

static struct evil_environment_t *
create_test_environment(void)
{
    size_t stack_size;
    size_t heap_size;

    stack_size = 1024 * sizeof(struct evil_object_t);
    heap_size = 1024 * 1024;
    stack = evil_aligned_alloc(sizeof(void *), stack_size);
    heap = evil_aligned_alloc(4096, heap_size);

    return evil_environment_create(stack, stack_size, heap, heap_size);
}

static void
destroy_test_environment(struct evil_environment_t *environment)
{
    evil_environment_destroy(environment);
    evil_aligned_free(stack);
    evil_aligned_free(heap);
}

static struct evil_object_handle_t *
create_string_object(struct evil_environment_t *environment, const char *test)
{
    size_t test_length;
    struct evil_object_t *string;
    struct evil_object_handle_t *string_handle;

    test_length = strlen(test);

    string = gc_alloc(environment->heap, TAG_STRING, test_length);
    memmove(string->value.string_value, test, test_length);
    string_handle = evil_create_object_handle(environment, string);

    return string_handle;
}

static struct evil_object_handle_t *
create_test_ast(struct evil_environment_t *environment, struct evil_object_handle_t *string_handle)
{
    struct evil_object_t *string_object;
    struct evil_object_t ast;

    string_object = evil_resolve_object_handle(string_handle);
    ast = evil_read(environment, 1, string_object);

    return evil_create_object_handle_from_value(environment, ast);
}

static struct evil_object_handle_t *
evaluate_test_result(struct evil_environment_t *environment, struct evil_object_handle_t *ast_handle)
{
    struct evil_object_t *ast_object;
    struct evil_object_t result;

    ast_object = evil_resolve_object_handle(ast_handle);
    result = evil_eval(environment, 1, ast_object);

    return evil_create_object_handle_from_value(environment, result);
}

static int
run_test(struct evil_environment_t *environment, const char *test, const char *expected)
{
    struct evil_object_handle_t *string_handle;
    struct evil_object_handle_t *ast_handle;
    struct evil_object_handle_t *result_handle;

    reset_print_buffer();

    string_handle = create_string_object(environment, test);
    ast_handle = create_test_ast(environment, string_handle);
    result_handle = evaluate_test_result(environment, ast_handle);

    evil_print(environment, 1, evil_resolve_object_handle(result_handle));

    evil_destroy_object_handle(environment, result_handle);
    evil_destroy_object_handle(environment, ast_handle);
    evil_destroy_object_handle(environment, string_handle);

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

    static uint64_t
    get_ticks(void)
    {
        uint64_t time;

        QueryPerformanceCounter((LARGE_INTEGER *)&time);

        return time;
    }

    static uint64_t
    get_tick_frequency(void)
    {
        uint64_t frequency;

        QueryPerformanceFrequency((LARGE_INTEGER *)&frequency);

        return frequency;
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

    static uint64_t
    get_ticks(void)
    {
#ifndef __APPLE__
        struct timespec clock_time;

        clock_gettime(CLOCK_REALTIME, &clock_time);

        return (clock_time.tv_sec * UINT64_C(1000000000)) + clock_time.tv_nsec;
#else
        return 0;
#endif
    }

    static uint64_t
    get_tick_frequency(void)
    {
        return UINT64_C(1000000000);
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
    }
    else
    {
        printf("%s PASSED\n", test_name);
    }
}

static int
should_run_test(const char *test, int num_filters, char **filters)
{
    int i;

    if (num_filters == 0)
    {
        return 1;
    }

    for (i = 0; i < num_filters; ++i)
    {
        if (strstr(test, filters[i]) != NULL)
        {
            return 1;
        }
    }

    return 0;
}

struct test_t
{
    uint64_t ticks;
    int success;
    int broken;
    char filename[260];
};

static int
test_comparer(const void *a, const void *b)
{
    const struct test_t *test_a;
    const struct test_t *test_b;

    test_a = a;
    test_b = b;

    return strcmp(test_a->filename, test_b->filename);
}

static struct test_t *
setup_benchmarks(int argc, char *argv[], struct test_t *old_tests, int *num_tests_ptr)
{
    int i;
    int num_tests;
    struct test_t *tests;
    struct test_t *p;

    tests = NULL;
    num_tests = *num_tests_ptr;

    if (argc < 2 || strcmp(argv[1], "-benchmark") != 0)
    {
        return old_tests;
    }


    tests = calloc(num_tests * NUM_BENCHMARK_ITERATIONS, sizeof(struct test_t));
    p = tests;

    for (i = 0; i < num_tests; ++i)
    {
        int ii;

        for (ii = 0; ii < NUM_BENCHMARK_ITERATIONS; ++ii)
        {
            memmove(p, old_tests + i, sizeof(struct test_t));
            ++p;
        }
    }

    *num_tests_ptr = num_tests * NUM_BENCHMARK_ITERATIONS;
    print_test_summary = print_benchmark_summary;

    return tests;
}

static struct test_t *
initialize_tests(const char *directory_name, int argc, char *argv[], int *num_tests_ptr)
{
    int num_tests;
    struct test_t *tests;
    directory_t dir;

    num_tests = 0;
    tests = NULL;
    dir = begin_directory_traversal(directory_name);

    if (!is_valid_directory(dir))
    {
        *num_tests_ptr = 0;
        return NULL;
    }

    for (;;)
    {
        char filename[260];

        memset(filename, 0, sizeof filename);

        if (get_next_file(dir, filename, sizeof filename))
        {
            break;
        }

        if (!should_run_test(filename, argc - 1, argv + 1))
        {
            continue;
        }

        tests = realloc(tests, (sizeof(struct test_t) * (num_tests + 1)));
        tests[num_tests].ticks = 0;
        tests[num_tests].success = 0;
        tests[num_tests].broken = 0;
        memmove(tests[num_tests].filename, filename, sizeof filename);

        ++num_tests;
    }

    qsort(tests, num_tests, sizeof(struct test_t), test_comparer);
    *num_tests_ptr = num_tests;

    end_directory_traversal(dir);

    tests = setup_benchmarks(argc, argv, tests, num_tests_ptr);

    return tests;
}

static void
print_unit_test_summary(struct test_t *tests, int num_tests)
{
    int num_passed;
    int num_broken;
    int i;
    size_t max_test_name_length;
    double ticks_to_ms;

    num_passed = 0;
    num_broken = 0;
    max_test_name_length = 0;
    ticks_to_ms = 1000.0 / (double)get_tick_frequency();

    for (i = 0; i < num_tests; ++i)
    {
        size_t test_name_length;

        test_name_length = strlen(tests[i].filename);
        max_test_name_length = MAX(test_name_length, max_test_name_length);
    }

    printf("\n========================================\n");

    for (i = 0; i < num_tests; ++i)
    {
        size_t test_name_length;
        double test_time;
        const char *status;

        test_name_length = strlen(tests[i].filename);
        test_time = (double)tests[i].ticks * ticks_to_ms;

        if (tests[i].broken)
        {
            status = "BROKEN";
            ++num_broken;
        }
        else
        {
            if (tests[i].success)
            {
                status = "passed";
                ++num_passed;
            }
            else
            {
                status = "FAILED";
            }
        }

        printf("    %s %*s %s (%.2f ms)\n",
                tests[i].filename,
                (int)(max_test_name_length - test_name_length),
                "",
                status,
                test_time);
    }

    printf("\npassed: %d\n", num_passed);
    printf("broken: %d\n", num_broken);
    printf("failed: %d\n", num_tests - num_passed - num_broken);
}

static void
print_benchmark_summary(struct test_t *tests, int num_tests)
{
    int i;
    int num_test_cases;
    double ticks_to_ms;

    num_test_cases = num_tests / NUM_BENCHMARK_ITERATIONS;
    ticks_to_ms = 1000.0 / (double)get_tick_frequency();

    printf("\n========================================\n");
    printf("Test, Iterations, Min, Max, Mean, Std Dev\n");

    for (i = 0; i < num_test_cases; ++i)
    {
        int ii;
        double total_ms;
        double mean;
        double sum_squares;
        double stddev;
        double min;
        double max;

        min = FLT_MAX;
        max = 0;
        total_ms = 0;

        for (ii = 0; ii < NUM_BENCHMARK_ITERATIONS; ++ii)
        {
            int idx;
            double ms;

            idx = (i * NUM_BENCHMARK_ITERATIONS) + ii;
            ms = tests[idx].ticks * ticks_to_ms;
            total_ms += ms;
            min = MIN(ms, min);
            max = MAX(ms, max);

            if (ii < (NUM_BENCHMARK_ITERATIONS - 1))
            {
                assert(strcmp(tests[idx].filename, tests[idx + 1].filename) == 0);
            }
        }

        mean = total_ms / (double)NUM_BENCHMARK_ITERATIONS;
        sum_squares = 0;

        for (ii = 0; ii < NUM_BENCHMARK_ITERATIONS; ++ii)
        {
            int idx;
            double ms;
            double difference;

            idx = (i * NUM_BENCHMARK_ITERATIONS) + ii;
            ms = tests[idx].ticks * ticks_to_ms;
            difference = ms - mean;

            sum_squares += difference * difference;
        }

        stddev = sqrt(sum_squares / (double)NUM_BENCHMARK_ITERATIONS);

        printf("%s,%d,%f,%f,%f,%f\n", tests[i * NUM_BENCHMARK_ITERATIONS].filename, NUM_BENCHMARK_ITERATIONS, min, max, mean, stddev);
    }
}

int
evil_run_tests(int argc, char *argv[])
{
    struct evil_environment_t *environment;
    int num_tests;
    int num_passed;
    int i;
    struct test_t *tests;

    #define TEST_DIR "tests"

    num_tests = 0;
    num_passed = 0;

    tests = initialize_tests(TEST_DIR, argc, argv, &num_tests);
    environment = create_test_environment();

    for (i = 0; i < num_tests; ++i)
    {
        const char *filename;
        char *test_file;
        char *test_end;
        char *test;
        char *expected;
        int result;
        uint64_t begin;
        uint64_t end;

        filename = tests[i].filename;
        test_file = read_test_file(filename);
        test_end = test_file + strlen(test_file);
        test = NULL;

        if (strstr(test_file, "#!broken") != NULL)
        {
            tests[i].broken = 1;
            goto next_test;
        }

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

        begin = get_ticks();
        result = run_test(environment, test, expected);
        end = get_ticks();
        report_test_result(filename, result, expected);
        tests[i].success = result;
        tests[i].ticks = end - begin;

        num_passed += result;

    next_test:
        if (test)
        {
            free(test);
        }
    }

    print_test_summary(tests, num_tests);

    destroy_test_environment(environment);
    free_print_buffer();
    free(tests);

    return num_tests - num_passed;
}
