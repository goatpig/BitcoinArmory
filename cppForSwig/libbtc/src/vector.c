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

#include <btc/vector.h>

#include <string.h>

vector* vector_new(size_t res, void (*free_f)(void*))
{
    vector* vec = btc_calloc(1, sizeof(vector));
    if (!vec)
        return NULL;

    vec->alloc = 8;
    while (vec->alloc < res)
        vec->alloc *= 2;

    vec->elem_free_f = free_f;
    vec->data = btc_malloc(vec->alloc * sizeof(void*));
    if (!vec->data) {
        btc_free(vec);
        return NULL;
    }

    return vec;
}

static void vector_free_data(vector* vec)
{
    if (!vec->data)
        return;

    if (vec->elem_free_f) {
        unsigned int i;
        for (i = 0; i < vec->len; i++)
            if (vec->data[i]) {
                vec->elem_free_f(vec->data[i]);
                vec->data[i] = NULL;
            }
    }

    btc_free(vec->data);
    vec->data = NULL;
    vec->alloc = 0;
    vec->len = 0;
}

void vector_free(vector* vec, btc_bool free_array)
{
    if (!vec)
        return;

    if (free_array)
        vector_free_data(vec);

    memset(vec, 0, sizeof(*vec));
    btc_free(vec);
}

static btc_bool vector_grow(vector* vec, size_t min_sz)
{
    size_t new_alloc = vec->alloc;
    while (new_alloc < min_sz)
        new_alloc *= 2;

    if (vec->alloc == new_alloc)
        return true;

    void* new_data = btc_realloc(vec->data, new_alloc * sizeof(void*));
    if (!new_data)
        return false;

    vec->data = new_data;
    vec->alloc = new_alloc;
    return true;
}

ssize_t vector_find(vector* vec, void* data)
{
    if (vec && vec->len) {
        size_t i;
        for (i = 0; i < vec->len; i++)
            if (vec->data[i] == data)
                return (ssize_t)i;
    }

    return -1;
}

btc_bool vector_add(vector* vec, void* data)
{
    if (vec->len == vec->alloc)
        if (!vector_grow(vec, vec->len + 1))
            return false;

    vec->data[vec->len] = data;
    vec->len++;
    return true;
}

void vector_remove_range(vector* vec, size_t pos, size_t len)
{
    if (!vec || ((pos + len) > vec->len))
        return;

    if (vec->elem_free_f) {
        unsigned int i, count;
        for (i = pos, count = 0; count < len; i++, count++)
            vec->elem_free_f(vec->data[i]);
    }

    memmove(&vec->data[pos], &vec->data[pos + len], (vec->len - pos - len) * sizeof(void*));
    vec->len -= len;
}

void vector_remove_idx(vector* vec, size_t pos)
{
    vector_remove_range(vec, pos, 1);
}

btc_bool vector_remove(vector* vec, void* data)
{
    ssize_t idx = vector_find(vec, data);
    if (idx < 0)
        return false;

    vector_remove_idx(vec, idx);
    return true;
}

btc_bool vector_resize(vector* vec, size_t newsz)
{
    unsigned int i;

    /* same size */
    if (newsz == vec->len)
        return true;

    /* truncate */
    else if (newsz < vec->len) {
        size_t del_count = vec->len - newsz;

        for (i = (vec->len - del_count); i < vec->len; i++) {
            if (vec->elem_free_f)
                vec->elem_free_f(vec->data[i]);
            vec->data[i] = NULL;
        }

        vec->len = newsz;
        return true;
    }

    /* last possibility: grow */
    if (!vector_grow(vec, newsz))
        return false;

    /* set new elements to NULL */
    for (i = vec->len; i < newsz; i++)
        vec->data[i] = NULL;

    return true;
}
