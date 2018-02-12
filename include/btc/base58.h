/**
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __LIBBTC_BASE58_H__
#define __LIBBTC_BASE58_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <stdint.h>

LIBBTC_API int btc_base58_encode_check(const uint8_t* data, int len, char* str, int strsize);
LIBBTC_API int btc_base58_decode_check(const char* str, uint8_t* data, size_t datalen);

LIBBTC_API int btc_base58_encode(char* b58, size_t* b58sz, const void* data, size_t binsz);
LIBBTC_API int btc_base58_decode(void* bin, size_t* binszp, const char* b58);


#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_BASE58_H__
