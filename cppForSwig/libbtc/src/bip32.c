/**
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 * Copyright (c) 2015 Douglas J. Bakkumk
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


#include <btc/bip32.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <btc/base58.h>
#include <btc/ecc.h>
#include <btc/ecc_key.h>
#include <btc/hash.h>
#include <btc/sha2.h>
#include <btc/utils.h>

#include "memory.h"

#include "ripemd160.h"

// write 4 big endian bytes
static void write_be(uint8_t* data, uint32_t x)
{
    data[0] = x >> 24;
    data[1] = x >> 16;
    data[2] = x >> 8;
    data[3] = x;
}


// read 4 big endian bytes
static uint32_t read_be(const uint8_t* data)
{
    return (((uint32_t)data[0]) << 24) |
           (((uint32_t)data[1]) << 16) |
           (((uint32_t)data[2]) << 8) |
           (((uint32_t)data[3]));
}

btc_hdnode* btc_hdnode_new()
{
    btc_hdnode* hdnode;
    hdnode = btc_calloc(1, sizeof(*hdnode));
    return hdnode;
}

btc_hdnode* btc_hdnode_copy(btc_hdnode* hdnode)
{
    btc_hdnode* newnode = btc_hdnode_new();

    newnode->depth = hdnode->depth;
    newnode->fingerprint = hdnode->fingerprint;
    newnode->child_num = hdnode->child_num;
    memcpy(newnode->chain_code, hdnode->chain_code, sizeof(hdnode->chain_code));
    memcpy(newnode->private_key, hdnode->private_key, sizeof(hdnode->private_key));
    memcpy(newnode->public_key, hdnode->public_key, sizeof(hdnode->public_key));

    return newnode;
}

void btc_hdnode_free(btc_hdnode* hdnode)
{
    memset(hdnode->chain_code, 0, sizeof(hdnode->chain_code));
    memset(hdnode->private_key, 0, sizeof(hdnode->private_key));
    memset(hdnode->public_key, 0, sizeof(hdnode->public_key));
    btc_free(hdnode);
}

btc_bool btc_hdnode_from_seed(const uint8_t* seed, int seed_len, btc_hdnode* out)
{
    uint8_t I[BTC_ECKEY_PKEY_LENGTH + BTC_BIP32_CHAINCODE_SIZE];
    memset(out, 0, sizeof(btc_hdnode));
    out->depth = 0;
    out->fingerprint = 0x00000000;
    out->child_num = 0;
    hmac_sha512((const uint8_t*)"Bitcoin seed", 12, seed, seed_len, I);
    memcpy(out->private_key, I, BTC_ECKEY_PKEY_LENGTH);

    if (!btc_ecc_verify_privatekey(out->private_key)) {
        memset(I, 0, sizeof(I));
        return false;
    }

    memcpy(out->chain_code, I + BTC_ECKEY_PKEY_LENGTH, BTC_BIP32_CHAINCODE_SIZE);
    btc_hdnode_fill_public_key(out);
    memset(I, 0, sizeof(I));
    return true;
}


btc_bool btc_hdnode_public_ckd(btc_hdnode* inout, uint32_t i)
{
    uint8_t data[1 + 32 + 4];
    uint8_t I[32 + BTC_BIP32_CHAINCODE_SIZE];
    uint8_t fingerprint[32];

    if (i & 0x80000000) { // private derivation
        return false;
    } else { // public derivation
        memcpy(data, inout->public_key, BTC_ECKEY_COMPRESSED_LENGTH);
    }
    write_be(data + BTC_ECKEY_COMPRESSED_LENGTH, i);

    sha256_Raw(inout->public_key, BTC_ECKEY_COMPRESSED_LENGTH, fingerprint);
    ripemd160(fingerprint, 32, fingerprint);
    inout->fingerprint = (fingerprint[0] << 24) + (fingerprint[1] << 16) + (fingerprint[2] << 8) + fingerprint[3];

    memset(inout->private_key, 0, 32);

    int failed = 0;
    hmac_sha512(inout->chain_code, 32, data, sizeof(data), I);
    memcpy(inout->chain_code, I + 32, BTC_BIP32_CHAINCODE_SIZE);


    if (!btc_ecc_public_key_tweak_add(inout->public_key, I))
        failed = false;

    if (!failed) {
        inout->depth++;
        inout->child_num = i;
    }

    // Wipe all stack data.
    memset(data, 0, sizeof(data));
    memset(I, 0, sizeof(I));
    memset(fingerprint, 0, sizeof(fingerprint));

    return failed ? false : true;
}


btc_bool btc_hdnode_private_ckd(btc_hdnode* inout, uint32_t i)
{
    uint8_t data[1 + BTC_ECKEY_PKEY_LENGTH + 4];
    uint8_t I[BTC_ECKEY_PKEY_LENGTH + BTC_BIP32_CHAINCODE_SIZE];
    uint8_t fingerprint[BTC_BIP32_CHAINCODE_SIZE];
    uint8_t p[BTC_ECKEY_PKEY_LENGTH], z[BTC_ECKEY_PKEY_LENGTH];

    if (i & 0x80000000) { // private derivation
        data[0] = 0;
        memcpy(data + 1, inout->private_key, BTC_ECKEY_PKEY_LENGTH);
    } else { // public derivation
        memcpy(data, inout->public_key, BTC_ECKEY_COMPRESSED_LENGTH);
    }
    write_be(data + BTC_ECKEY_COMPRESSED_LENGTH, i);

    sha256_Raw(inout->public_key, BTC_ECKEY_COMPRESSED_LENGTH, fingerprint);
    ripemd160(fingerprint, 32, fingerprint);
    inout->fingerprint = (fingerprint[0] << 24) + (fingerprint[1] << 16) +
                         (fingerprint[2] << 8) + fingerprint[3];

    memset(fingerprint, 0, sizeof(fingerprint));
    memcpy(p, inout->private_key, BTC_ECKEY_PKEY_LENGTH);

    hmac_sha512(inout->chain_code, BTC_BIP32_CHAINCODE_SIZE, data, sizeof(data), I);
    memcpy(inout->chain_code, I + BTC_ECKEY_PKEY_LENGTH, BTC_BIP32_CHAINCODE_SIZE);
    memcpy(inout->private_key, I, BTC_ECKEY_PKEY_LENGTH);

    memcpy(z, inout->private_key, BTC_ECKEY_PKEY_LENGTH);

    int failed = 0;
    if (!btc_ecc_verify_privatekey(z)) {
        failed = 1;
        return false;
    }

    memcpy(inout->private_key, p, BTC_ECKEY_PKEY_LENGTH);
    if (!btc_ecc_private_key_tweak_add(inout->private_key, z)) {
        failed = 1;
    }

    if (!failed) {
        inout->depth++;
        inout->child_num = i;
        btc_hdnode_fill_public_key(inout);
    }

    memset(data, 0, sizeof(data));
    memset(I, 0, sizeof(I));
    memset(p, 0, sizeof(p));
    memset(z, 0, sizeof(z));
    return true;
}


void btc_hdnode_fill_public_key(btc_hdnode* node)
{
    size_t outsize = BTC_ECKEY_COMPRESSED_LENGTH;
    btc_ecc_get_pubkey(node->private_key, node->public_key, &outsize, true);
}


static void btc_hdnode_serialize(const btc_hdnode* node, uint32_t version, char use_public, char* str, int strsize)
{
    uint8_t node_data[78];
    write_be(node_data, version);
    node_data[4] = node->depth;
    write_be(node_data + 5, node->fingerprint);
    write_be(node_data + 9, node->child_num);
    memcpy(node_data + 13, node->chain_code, BTC_BIP32_CHAINCODE_SIZE);
    if (use_public) {
        memcpy(node_data + 45, node->public_key, BTC_ECKEY_COMPRESSED_LENGTH);
    } else {
        node_data[45] = 0;
        memcpy(node_data + 46, node->private_key, BTC_ECKEY_PKEY_LENGTH);
    }
    btc_base58_encode_check(node_data, 78, str, strsize);
}


void btc_hdnode_serialize_public(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize)
{
    btc_hdnode_serialize(node, chain->b58prefix_bip32_pubkey, 1, str, strsize);
}


void btc_hdnode_serialize_private(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize)
{
    btc_hdnode_serialize(node, chain->b58prefix_bip32_privkey, 0, str, strsize);
}


void btc_hdnode_get_hash160(const btc_hdnode* node, uint160 hash160_out)
{
    uint256 hashout;
    btc_hash_sngl_sha256(node->public_key, BTC_ECKEY_COMPRESSED_LENGTH, hashout);
    ripemd160(hashout, sizeof(hashout), hash160_out);
}

void btc_hdnode_get_p2pkh_address(const btc_hdnode* node, const btc_chainparams* chain, char* str, int strsize)
{
    uint8_t hash160[sizeof(uint160)+1];
    hash160[0] = chain->b58prefix_pubkey_address;
    btc_hdnode_get_hash160(node, hash160 + 1);
    btc_base58_encode_check(hash160, sizeof(hash160), str, strsize);
}

btc_bool btc_hdnode_get_pub_hex(const btc_hdnode* node, char* str, size_t* strsize)
{
    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    memcpy(&pubkey.pubkey, node->public_key, BTC_ECKEY_COMPRESSED_LENGTH);
    pubkey.compressed = true;

    return btc_pubkey_get_hex(&pubkey, str, strsize);
}


// check for validity of curve point in case of public data not performed
btc_bool btc_hdnode_deserialize(const char* str, const btc_chainparams* chain, btc_hdnode* node)
{
    uint8_t node_data[strlen(str)];
    memset(node, 0, sizeof(btc_hdnode));
    size_t outlen = 0;

    outlen = btc_base58_decode_check(str, node_data, sizeof(node_data));
    if (!outlen) {
        return false;
    }
    uint32_t version = read_be(node_data);
    if (version == chain->b58prefix_bip32_pubkey) { // public node
        memcpy(node->public_key, node_data + 45, BTC_ECKEY_COMPRESSED_LENGTH);
    } else if (version == chain->b58prefix_bip32_privkey) { // private node
        if (node_data[45]) {                                // invalid data
            return false;
        }
        memcpy(node->private_key, node_data + 46, BTC_ECKEY_PKEY_LENGTH);
        btc_hdnode_fill_public_key(node);
    } else {
        return false; // invalid version
    }
    node->depth = node_data[4];
    node->fingerprint = read_be(node_data + 5);
    node->child_num = read_be(node_data + 9);
    memcpy(node->chain_code, node_data + 13, BTC_BIP32_CHAINCODE_SIZE);
    return true;
}

btc_bool btc_hd_generate_key(btc_hdnode* node, const char* keypath, const uint8_t* keymaster, const uint8_t* chaincode, btc_bool usepubckd)
{
    static char delim[] = "/";
    static char prime[] = "phH\'";
    static char digits[] = "0123456789";
    uint64_t idx = 0;
    assert(strlens(keypath) < 1024);
    char *pch, *kp = btc_malloc(strlens(keypath) + 1);

    if (!kp) {
        return false;
    }

    if (strlens(keypath) < strlens("m/")) {
        goto err;
    }

    memset(kp, 0, strlens(keypath) + 1);
    memcpy(kp, keypath, strlens(keypath));

    if (kp[0] != 'm' || kp[1] != '/') {
        goto err;
    }

    node->depth = 0;
    node->child_num = 0;
    node->fingerprint = 0;
    memcpy(node->chain_code, chaincode, BTC_BIP32_CHAINCODE_SIZE);
    if (usepubckd == true) {
        memcpy(node->public_key, keymaster, BTC_ECKEY_COMPRESSED_LENGTH);
    } else {
        memcpy(node->private_key, keymaster, BTC_ECKEY_PKEY_LENGTH);
        btc_hdnode_fill_public_key(node);
    }

    pch = strtok(kp + 2, delim);
    while (pch != NULL) {
        size_t i = 0;
        int prm = 0;
        for (; i < strlens(pch); i++) {
            if (strchr(prime, pch[i])) {
                if ((i != strlens(pch) - 1) || usepubckd == true) {
                    goto err;
                }
                prm = 1;
            } else if (!strchr(digits, pch[i])) {
                goto err;
            }
        }

        idx = strtoull(pch, NULL, 10);
        if (idx > UINT32_MAX) {
            goto err;
        }

        if (prm) {
            if (btc_hdnode_private_ckd_prime(node, idx) != true) {
                goto err;
            }
        } else {
            if ((usepubckd == true ? btc_hdnode_public_ckd(node, idx) : btc_hdnode_private_ckd(node, idx)) != true) {
                goto err;
            }
        }
        pch = strtok(NULL, delim);
    }
    btc_free(kp);
    return true;

err:
    btc_free(kp);
    return false;
}

btc_bool btc_hdnode_has_privkey(btc_hdnode* node)
{
    int i;
    for (i = 0; i < BTC_ECKEY_PKEY_LENGTH; ++i) {
        if (node->private_key[i] != 0)
            return true;
    }
    return false;
}
