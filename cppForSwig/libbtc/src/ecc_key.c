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

#include <btc/ecc_key.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <btc/base58.h>
#include <btc/chainparams.h>
#include <btc/ecc.h>
#include <btc/hash.h>
#include <btc/random.h>
#include <btc/script.h>
#include <btc/segwit_addr.h>
#include <btc/utils.h>

#include "ripemd160.h"


void btc_privkey_init(btc_key* privkey)
{
    memset(&privkey->privkey, 0, BTC_ECKEY_PKEY_LENGTH);
}


btc_bool btc_privkey_is_valid(const btc_key* privkey)
{
    if (!privkey) {
        return false;
    }
    return btc_ecc_verify_privatekey(privkey->privkey);
}


void btc_privkey_cleanse(btc_key* privkey)
{
    btc_mem_zero(&privkey->privkey, BTC_ECKEY_PKEY_LENGTH);
}


void btc_privkey_gen(btc_key* privkey)
{
    if (privkey == NULL)
        return;

    do {
        assert(btc_random_bytes(privkey->privkey, BTC_ECKEY_PKEY_LENGTH, 0));
    } while (btc_ecc_verify_privatekey(privkey->privkey) == 0);
}


btc_bool btc_privkey_verify_pubkey(btc_key* privkey, btc_pubkey* pubkey)
{
    uint256 rnddata, hash;
    assert(btc_random_bytes(rnddata, BTC_HASH_LENGTH, 0));
    btc_hash(rnddata, BTC_HASH_LENGTH, hash);

    unsigned char sig[74];
    size_t siglen = 74;

    if (!btc_key_sign_hash(privkey, hash, sig, &siglen))
        return false;

    return btc_pubkey_verify_sig(pubkey, hash, sig, siglen);
}

void btc_privkey_encode_wif(const btc_key* privkey, const btc_chainparams* chain, char *privkey_wif, size_t *strsize_inout) {
    uint8_t pkeybase58c[34];
    pkeybase58c[0] = chain->b58prefix_secret_address;
    pkeybase58c[33] = 1; /* always use compressed keys */

    memcpy(&pkeybase58c[1], privkey->privkey, BTC_ECKEY_PKEY_LENGTH);
    assert(btc_base58_encode_check(pkeybase58c, 34, privkey_wif, *strsize_inout) != 0);
    btc_mem_zero(&pkeybase58c, 34);
}

btc_bool btc_privkey_decode_wif(const char *privkey_wif, const btc_chainparams* chain, btc_key* privkey) {

    if (!privkey_wif || strlen(privkey_wif) < 50) {
        return false;
    }
    uint8_t privkey_data[strlen(privkey_wif)];
    memset(privkey_data, 0, sizeof(privkey_data));
    size_t outlen = 0;

    outlen = btc_base58_decode_check(privkey_wif, privkey_data, sizeof(privkey_data));
    if (!outlen) {
        return false;
    }
    if (privkey_data[0] != chain->b58prefix_secret_address) {
        return false;
    }
    memcpy(privkey->privkey, &privkey_data[1], BTC_ECKEY_PKEY_LENGTH);
    btc_mem_zero(&privkey_data, sizeof(privkey_data));
    return true;
}

void btc_pubkey_init(btc_pubkey* pubkey)
{
    if (pubkey == NULL)
        return;

    memset(pubkey->pubkey, 0, BTC_ECKEY_UNCOMPRESSED_LENGTH);
    pubkey->compressed = false;
}


btc_bool btc_pubkey_is_valid(const btc_pubkey* pubkey)
{
    return btc_ecc_verify_pubkey(pubkey->pubkey, pubkey->compressed);
}


void btc_pubkey_cleanse(btc_pubkey* pubkey)
{
    if (pubkey == NULL)
        return;

    btc_mem_zero(pubkey->pubkey, BTC_ECKEY_UNCOMPRESSED_LENGTH);
}


void btc_pubkey_get_hash160(const btc_pubkey* pubkey, uint160 hash160)
{
    uint256 hashout;
    btc_hash_sngl_sha256(pubkey->pubkey, pubkey->compressed ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH, hashout);

    ripemd160(hashout, sizeof(hashout), hash160);
}


btc_bool btc_pubkey_get_hex(const btc_pubkey* pubkey, char* str, size_t* strsize)
{
    if (*strsize < BTC_ECKEY_COMPRESSED_LENGTH * 2)
        return false;
    utils_bin_to_hex((unsigned char*)pubkey->pubkey, BTC_ECKEY_COMPRESSED_LENGTH, str);
    *strsize = BTC_ECKEY_COMPRESSED_LENGTH * 2;
    return true;
}


void btc_pubkey_from_key(const btc_key* privkey, btc_pubkey* pubkey_inout)
{
    if (pubkey_inout == NULL || privkey == NULL)
        return;

    size_t in_out_len = BTC_ECKEY_COMPRESSED_LENGTH;

    btc_ecc_get_pubkey(privkey->privkey, pubkey_inout->pubkey, &in_out_len, true);
    pubkey_inout->compressed = true;
}


btc_bool btc_key_sign_hash(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen)
{
    return btc_ecc_sign(privkey->privkey, hash, sigout, outlen);
}


btc_bool btc_key_sign_hash_compact(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen)
{
    return btc_ecc_sign_compact(privkey->privkey, hash, sigout, outlen);
}

btc_bool btc_key_sign_hash_compact_recoverable(const btc_key* privkey, const uint256 hash, unsigned char* sigout, size_t* outlen, int* recid)
{
    return btc_ecc_sign_compact_recoverable(privkey->privkey, hash, sigout, outlen, recid);
}

btc_bool btc_key_sign_recover_pubkey(const unsigned char* sig, const uint256 hash, int recid, btc_pubkey* pubkey)
{
    uint8_t pubkeybuf[128];
    size_t outlen = 128;
    if (!btc_ecc_recover_pubkey(sig, hash, recid, pubkeybuf, &outlen) || outlen > BTC_ECKEY_UNCOMPRESSED_LENGTH) {
        return 0;
    }
    memset(pubkey->pubkey, 0, sizeof(pubkey->pubkey));
    memcpy(pubkey->pubkey, pubkeybuf, outlen);
    if (outlen == BTC_ECKEY_COMPRESSED_LENGTH) {
        pubkey->compressed = true;
    }
    return 1;
}

btc_bool btc_pubkey_verify_sig(const btc_pubkey* pubkey, const uint256 hash, unsigned char* sigder, int len)
{
    return btc_ecc_verify_sig(pubkey->pubkey, pubkey->compressed, hash, sigder, len);
}

btc_bool btc_pubkey_getaddr_p2sh_p2wpkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout) {
    cstring *p2wphk_script = cstr_new_sz(22);
    uint160 keyhash;
    btc_pubkey_get_hash160(pubkey, keyhash);
    btc_script_build_p2wpkh(p2wphk_script, keyhash);

    uint8_t hash160[sizeof(uint160)+1];
    hash160[0] = chain->b58prefix_script_address;
    btc_script_get_scripthash(p2wphk_script, hash160+1);
    cstr_free(p2wphk_script, true);

    btc_base58_encode_check(hash160, sizeof(hash160), addrout, 100);
    return true;
}

btc_bool btc_pubkey_getaddr_p2pkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout) {
    uint8_t hash160[sizeof(uint160)+1];
    hash160[0] = chain->b58prefix_pubkey_address;
    btc_pubkey_get_hash160(pubkey, hash160 + 1);
    btc_base58_encode_check(hash160, sizeof(hash160), addrout, 100);
    return true;
}

btc_bool btc_pubkey_getaddr_p2wpkh(const btc_pubkey* pubkey, const btc_chainparams* chain, char *addrout) {
    uint160 hash160;
    btc_pubkey_get_hash160(pubkey, hash160);
    segwit_addr_encode(addrout, chain->bech32_hrp, 0, hash160, sizeof(hash160));
    return true;
}
