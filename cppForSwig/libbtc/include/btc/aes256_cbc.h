/*
 The MIT License (MIT)

 Copyright (c) 2016 Jonas Schnelli

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

#ifndef __LIBBTC_AES256_CBC_H_
#define __LIBBTC_AES256_CBC_H_

#define AES_BLOCK_SIZE 16

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <stddef.h>

LIBBTC_API int aes256_cbc_encrypt(const unsigned char aes_key[32], const unsigned char iv[AES_BLOCK_SIZE], const unsigned char* data, int size, int pad, unsigned char* out);
LIBBTC_API int aes256_cbc_decrypt(const unsigned char aes_key[32], const unsigned char iv[AES_BLOCK_SIZE], const unsigned char* data, int size, int pad, unsigned char* out);

#ifdef __cplusplus
}
#endif

#endif /* __LIBBTC_AES256_CBC_H_ */
