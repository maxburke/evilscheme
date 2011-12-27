#ifndef BASE_H
#define BASE_H

#ifdef _MSC_VER
#   include <Windows.h>
#   define BREAK() __asm int 3
#else
#   include <signal.h>
#   define BREAK() raise(SIGTRAP)
#endif

#define UNUSED(x) (void)x

#endif
