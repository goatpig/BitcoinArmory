/*

 The MIT License (MIT)

 Copyright (c) 2017 Jonas Schnelli

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef __LIBBTC_MEMORY_H__
#define __LIBBTC_MEMORY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <stddef.h>

typedef struct btc_mem_mapper_ {
    void* (*btc_malloc)(size_t size);
    void* (*btc_calloc)(size_t count, size_t size);
    void* (*btc_realloc)(void* ptr, size_t size);
    void (*btc_free)(void* ptr);
} btc_mem_mapper;

// set's a custom memory mapper
// this function is _not_ thread safe and must be called before anything else
LIBBTC_API void btc_mem_set_mapper(const btc_mem_mapper mapper);
LIBBTC_API void btc_mem_set_mapper_default();

LIBBTC_API void* btc_malloc(size_t size);
LIBBTC_API void* btc_calloc(size_t count, size_t size);
LIBBTC_API void* btc_realloc(void* ptr, size_t size);
LIBBTC_API void btc_free(void* ptr);

LIBBTC_API volatile void *btc_mem_zero(volatile void *dst, size_t len);

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_MEMORY_H__
