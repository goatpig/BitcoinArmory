/*

 The MIT License (MIT)

 Copyright (c) 2015 Jonas Schnelli

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

#ifndef __LIBBTC_VECTOR_H__
#define __LIBBTC_VECTOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <stdlib.h>
#include <sys/types.h>

typedef struct vector {
    void** data;  /* array of pointers */
    size_t len;   /* array element count */
    size_t alloc; /* allocated array elements */

    void (*elem_free_f)(void*);
} vector;

LIBBTC_API vector* vector_new(size_t res, void (*free_f)(void*));
LIBBTC_API void vector_free(vector* vec, btc_bool free_array);

LIBBTC_API btc_bool vector_add(vector* vec, void* data);
LIBBTC_API btc_bool vector_remove(vector* vec, void* data);
LIBBTC_API void vector_remove_idx(vector* vec, size_t idx);
LIBBTC_API void vector_remove_range(vector* vec, size_t idx, size_t len);
LIBBTC_API btc_bool vector_resize(vector* vec, size_t newsz);

LIBBTC_API ssize_t vector_find(vector* vec, void* data);

#define vector_idx(vec, idx) ((vec)->data[(idx)])

#ifdef __cplusplus
}
#endif

#endif /* __LIBBTC_VECTOR_H__ */
