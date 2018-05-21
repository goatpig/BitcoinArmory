/*

 The MIT License (MIT)

 Copyright (c) 2016 Thomas Kerin
 Copyright (c) 2016 libbtc developers

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

#ifndef LIBBTC_BLOCK_H
#define LIBBTC_BLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include "buffer.h"
#include "cstr.h"
#include "hash.h"

#include <stddef.h>

typedef struct btc_block_header_ {
    int32_t version;
    uint256 prev_block;
    uint256 merkle_root;
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
} btc_block_header;

LIBBTC_API btc_block_header* btc_block_header_new();
LIBBTC_API void btc_block_header_free(btc_block_header* header);
LIBBTC_API int btc_block_header_deserialize(btc_block_header* header, struct const_buffer* buf);
LIBBTC_API void btc_block_header_serialize(cstring* s, const btc_block_header* header);
LIBBTC_API void btc_block_header_copy(btc_block_header* dest, const btc_block_header* src);
LIBBTC_API btc_bool btc_block_header_hash(btc_block_header* header, uint256 hash);

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_BLOCK_H__
