#ifndef FAST_FAIL_H
#define FAST_FAIL_H 1

#include <stdio.h>
#include <signal.h>
#include "stringify.h"

//Can only print constant strings.
#define FAST_FAIL(x)                                                    \
    do {                                                                \
        fprintf(stderr, __FILE__ ":" TO_STRING(__LINE__) " - " x "\n"); \
        raise(SIGABRT);                                                 \
    } while (0)

#endif

