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

#ifndef __LIBBTC_ECC_KEY_H__
#define __LIBBTC_ECC_KEY_H__

#include "btc.h"
#include "chainparams.h"

LIBBTC_BEGIN_DECL

typedef struct btc_key_ {
    uint8_t privkey[BTC_ECKEY_PKEY_LENGTH];
} btc_key;

typedef struct btc_pubkey_ {
    btc_bool compressed;
    uint8_t pubkey[BTC_ECKEY_UNCOMPRESSED_LENGTH];
} btc_pubkey;

LIBBTC_API void btc_privkey_init(btc_key* privkey);
LIBBTC_API btc_bool btc_privkey_is_valid(const btc_key* privkey);
LIBBTC_API void btc_privkey_cleanse(btc_key* privkey);
LIBBTC_API void btc_privkey_gen(btc_key* privkey);
LIBBTC_API btc_bool btc_privkey_verify_pubkey(btc_key* privkey, btc_pubkey* pubkey);

// form a WIF encoded string from the given pubkey, make sure privkey_wif is large enough and strsize_inout contains the size of the buffer
LIBBTC_API void btc_privkey_encode_wif(const btc_key* privkey, const btc_chainparams* chain, char *privkey_wif, size_t *strsize_inout);
LIBBTC_API btc_bool btc_privkey_decode_wif(const char *privkey_wif, const btc_chainparams* chain, btc_key* privkey);

LIBBTC_API void btc_pubkey_init(btc_pubkey* pubkey);

// Compute the length of a pubkey with a given first byte.
LIBBTC_API unsigned int btc_pubkey_get_length(unsigned char ch_header);

LIBBTC_API btc_bool btc_pubkey_is_valid(const btc_pubkey* pubkey);
LIBBTC_API void btc_pubkey_cleanse(btc_pubkey* pubkey);
LIBBTC_API void btc_pubkey_from_key(const btc_key* privkey, btc_pubkey* pubkey_inout);

//get the hash160 (single SHA256 + RIPEMD160)
LIBBTC_API void btc_pubkey_get_hash160(const btc_pubkey* pubkey, uint160 hash160);

//get the hex representation of a pubkey, strsize must be at leat 66 bytes
LIBBTC_API btc_bool btc_pubkey_get_hex(const btc_pubkey* pubkey, char* str, size_t* strsize);

//sign a 32byte message/hash and returns a DER encoded signature (through *sigout)
LIBBTC_API btc_bool btc_key_sign_hash(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen);

//sign a 32byte message/hash and returns a 64 byte compact signature (through *sigout)
LIBBTC_API btc_bool btc_key_sign_hash_compact(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen);

//sign a 32byte message/hash and returns a 64 byte compact signature (through *sigout) plus a 1byte recovery id
LIBBTC_API btc_bool btc_key_sign_hash_compact_recoverable(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen, int *recid);

LIBBTC_API btc_bool btc_key_sign_recover_pubkey(const unsigned char* sig, const uint256 hash, int recid, btc_pubkey* pubkey);

//verifies a DER encoded signature with given pubkey and return true if valid
LIBBTC_API btc_bool btc_pubkey_verify_sig(const btc_pubkey* pubkey, const uint256 hash, unsigned char* sigder, int len);

LIBBTC_API btc_bool btc_pubkey_getaddr_p2sh_p2wpkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout);
LIBBTC_API btc_bool btc_pubkey_getaddr_p2pkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout);
LIBBTC_API btc_bool btc_pubkey_getaddr_p2wpkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout);

LIBBTC_END_DECL

#endif // __LIBBTC_ECC_KEY_H__
