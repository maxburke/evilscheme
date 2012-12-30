#ifndef BASE_H
#define BASE_H

#ifdef _MSC_VER
#   define BREAK() __asm int 3
#else
#   include <signal.h>
#   define BREAK() raise(SIGTRAP)
#endif

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

#define UNUSED(x) (void)x

#endif
