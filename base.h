#ifndef BASE_H
#define BASE_H

/*
 * Pragma wrappers.
 */
#ifdef _MSC_VER
    #define DISABLE_WARNING(x) __pragma(warning(disable:x))
#else
    #define DISABLE_WARNING(x)
#endif

/*
 * Debug break macro.
 */
#ifdef _MSC_VER
#   define BREAK() __debugbreak()
#else
#   include <signal.h>
#   define BREAK() raise(SIGTRAP)
#endif

/*
 * Wrappers around the inline keyword.
 */
#if defined(_MSC_VER)
    /*
     * CL doesn't provide inline but it provides __inline
     */
    #define inline __inline
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    /*
     * C99 compliant compiler does not need inline to be defined.
     */
#else
    /*
     * GCC in C89 mode doesn't have inline but it does have __inline__
     */
    #define inline __inline__
#endif

/*
 * Formatting strings for systems that don't have inttypes.h
 */
#ifdef _MSC_VER
    #define PRId64 "I64d"
#else
    #include <inttypes.h>
#endif

#define UNUSED(x) (void)x

#endif
