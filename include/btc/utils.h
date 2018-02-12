/*

 The MIT License (MIT)

 Copyright (c) 2015 Douglas J. Bakkum
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

#ifndef __LIBBTC_UTILS_H__
#define __LIBBTC_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"
#include <btc/cstr.h>

#include <stddef.h>
#include <stdint.h>

#define TO_UINT8_HEX_BUF_LEN 2048
#define VARINT_LEN 20

#define strlens(s) (s == NULL ? 0 : strlen(s))

LIBBTC_API void utils_clear_buffers(void);
LIBBTC_API void utils_hex_to_bin(const char* str, unsigned char* out, int inLen, int* outLen);
LIBBTC_API void utils_bin_to_hex(unsigned char* bin_in, size_t inlen, char* hex_out);
LIBBTC_API uint8_t* utils_hex_to_uint8(const char* str);
LIBBTC_API char* utils_uint8_to_hex(const uint8_t* bin, size_t l);
LIBBTC_API void utils_reverse_hex(char* h, int len);
LIBBTC_API void utils_uint256_sethex(char* psz, uint8_t* out);
LIBBTC_API void* safe_malloc(size_t size);
LIBBTC_API void btc_cheap_random_bytes(uint8_t* buf, uint32_t len);
LIBBTC_API void btc_get_default_datadir(cstring *path_out);
LIBBTC_API void btc_file_commit(FILE *file);


/* support substitude for GNU only tdestroy */
/* Let's hope the node struct is always compatible */

struct btc_btree_node {
    void *key;
    struct btc_btree_node *left;
    struct btc_btree_node *right;
};

static inline void btc_btree_tdestroy(void *root, void (*freekey)(void *))
{
    struct btc_btree_node *r = root;

    if (r == 0)
        return;
    btc_btree_tdestroy(r->left, freekey);
    btc_btree_tdestroy(r->right, freekey);

    if (freekey) freekey(r->key);
    btc_free(r);
}

#ifdef __cplusplus
}
#endif

#endif /* __LIBBTC_UTILS_H__*/
