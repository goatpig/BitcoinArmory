/**
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 * Copyright (c) 2015 Douglas J. Bakkumk
 * Copyright (c) 2015 Jonas Schnelli
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

#ifndef __LIBBTC_BIP32_H__
#define __LIBBTC_BIP32_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"
#include "chainparams.h"

#include <stdint.h>

#define BTC_BIP32_CHAINCODE_SIZE 32

typedef struct
{
    uint32_t depth;
    uint32_t fingerprint;
    uint32_t child_num;
    uint8_t chain_code[BTC_BIP32_CHAINCODE_SIZE];
    uint8_t private_key[BTC_ECKEY_PKEY_LENGTH];
    uint8_t public_key[BTC_ECKEY_COMPRESSED_LENGTH];
} btc_hdnode;


#define btc_hdnode_private_ckd_prime(X, I) btc_hdnode_private_ckd((X), ((I) | 0x80000000))


LIBBTC_API btc_hdnode* btc_hdnode_new();
LIBBTC_API btc_hdnode* btc_hdnode_copy(btc_hdnode* hdnode);
LIBBTC_API void btc_hdnode_free(btc_hdnode* node);
LIBBTC_API btc_bool btc_hdnode_public_ckd(btc_hdnode* inout, uint32_t i);
LIBBTC_API btc_bool btc_hdnode_from_seed(const uint8_t* seed, int seed_len, btc_hdnode* out);
LIBBTC_API btc_bool btc_hdnode_private_ckd(btc_hdnode* inout, uint32_t i);
LIBBTC_API void btc_hdnode_fill_public_key(btc_hdnode* node);
LIBBTC_API void btc_hdnode_serialize_public(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize);
LIBBTC_API void btc_hdnode_serialize_private(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize);

/* gives out the raw sha256/ripemd160 hash */
LIBBTC_API void btc_hdnode_get_hash160(const btc_hdnode* node, uint160 hash160_out);
LIBBTC_API void btc_hdnode_get_p2pkh_address(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize);
LIBBTC_API btc_bool btc_hdnode_get_pub_hex(const btc_hdnode* node, char* str, size_t* strsize);
LIBBTC_API btc_bool btc_hdnode_deserialize(const char* str, const btc_chainparams* chain, btc_hdnode* node);

//!derive btc_hdnode from extended private or extended public key orkey
//if you use pub child key derivation, pass usepubckd=true
LIBBTC_API btc_bool btc_hd_generate_key(btc_hdnode* node, const char* keypath, const uint8_t* keymaster, const uint8_t* chaincode, btc_bool usepubckd);

//!checks if a node has the according private key (or if its a pubkey only node)
LIBBTC_API btc_bool btc_hdnode_has_privkey(btc_hdnode* node);

#ifdef __cplusplus
}
#endif

#endif // __LIBBTC_BIP32_H__
