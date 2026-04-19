#ifndef TESTS_QUIRC_COMPAT_ARDUINO_H_
#define TESTS_QUIRC_COMPAT_ARDUINO_H_

#include <stdlib.h>

static inline void *ps_malloc(size_t size) {
    return malloc(size);
}

#endif
