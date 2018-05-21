/**********************************************************************
 * Copyright (c) 2015 Jonas Schnelli                                  *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btc/block.h>

#include <btc/cstr.h>
#include <btc/ecc_key.h>
#include <btc/utils.h>

#include "utest.h"

struct blockheadertest {
    char hexheader[160];
    char hexhash[64];
    int32_t version;
    uint32_t timestamp;
    uint32_t bits;
    uint32_t nonce;
};

static const struct blockheadertest block_header_tests[] =
        {
                {"0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c", "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", 1, 1231006505, 486604799, 2083236893},
                {"04000000db716ecedcc0eabee8f8a333aeaca8d020c10b7d6c851baf1f97442d68397d6e376353ed67b819ae38f8e821a8f3bc51f28e57973e11ceff0191353a07b68285fac6ca56ffff7f2002000000", "0eba32fd89d25ff24ae4eb9c9f7dccd5032cbbaa5fedfd06c9d5b75e5edb7f2f", 4, 1456129786, 545259519, 2}
        };

void test_block_header()
{
    int outlen;
    char hexbuf[161];
    unsigned int i;
    for (i = 0; i < (sizeof(block_header_tests) / sizeof(block_header_tests[0])); i++) {
        cstring* serialized = cstr_new_sz(80);
        const struct blockheadertest* test = &block_header_tests[i];
        uint8_t header_data[80];
        uint256 hash_data;

        utils_hex_to_bin(test->hexheader, header_data, 160, &outlen);
        utils_hex_to_bin(test->hexhash, hash_data, sizeof(hash_data), &outlen);

        btc_block_header* header = btc_block_header_new();
        struct const_buffer buf = {header_data, 80};
        btc_block_header_deserialize(header, &buf);

        // Check the copies are the same
        btc_block_header* header_copy = btc_block_header_new();
        btc_block_header_copy(header_copy, header);
        assert(memcmp(header_copy, header, sizeof(*header_copy)) == 0);

        // Check the serialized form matches
        btc_block_header_serialize(serialized, header);
        utils_bin_to_hex((unsigned char*) serialized->str, serialized->len, hexbuf);
        assert(memcmp(hexbuf, test->hexheader, 160) == 0);

        // Check the block hash
        uint256 blockhash;
        btc_block_header_hash(header, blockhash);

        utils_bin_to_hex(blockhash, BTC_HASH_LENGTH, hexbuf);
        utils_reverse_hex(hexbuf, BTC_HASH_LENGTH*2);
        assert(memcmp(hexbuf, test->hexhash, BTC_HASH_LENGTH*2) == 0);

        // Check version, ts, bits, nonce
        assert(header->version == test->version);
        assert(header->timestamp == test->timestamp);
        assert(header->bits == test->bits);
        assert(header->nonce == test->nonce);

        btc_block_header_free(header);
        btc_block_header_free(header_copy);
        cstr_free(serialized, true);
    }


    /* blockheader */
    btc_block_header bheader, bheaderprev, bheadercheck;
    bheader.version = 536870912;
    bheader.timestamp = 1472802860;
    bheader.nonce = 2945279651;
    bheader.bits = 402979592;
    char *prevblock_hex_o = "00000000000000000098ad436f0c305b4d577e40e2687783822a2fe6637dc8e5";
    char *prevblock_hex = btc_malloc(strlen(prevblock_hex_o)+1);
    memcpy(prevblock_hex, prevblock_hex_o, strlen(prevblock_hex_o));
    utils_reverse_hex(prevblock_hex, 64);
    outlen = 0;
    utils_hex_to_bin(prevblock_hex, bheader.prev_block, 64, &outlen);
    btc_free(prevblock_hex);

    char *merkleroot_hex_o = "d4690e152bb72a3dc2a2a90f3f1e8afc3b48a26a070f2b099b46a439c69eb776";
    char *merkleroot_hex = btc_malloc(strlen(merkleroot_hex_o)+1);
    memcpy(merkleroot_hex, merkleroot_hex_o, strlen(merkleroot_hex_o));
    utils_reverse_hex(merkleroot_hex, 64);
    outlen = 0;
    utils_hex_to_bin(merkleroot_hex, bheader.merkle_root, 64, &outlen);
    btc_free(merkleroot_hex);

    bheaderprev.version = 536870912;
    bheaderprev.timestamp = 1472802636;
    bheaderprev.nonce = 3627526227;
    bheaderprev.bits = 402979592;

    prevblock_hex_o = "000000000000000001beee80fe573d34a51b48f30248a8933dc71b67db9f542d";
    prevblock_hex = btc_malloc(strlen(prevblock_hex_o)+1);
    memcpy(prevblock_hex, prevblock_hex_o, strlen(prevblock_hex_o));
    utils_reverse_hex(prevblock_hex, 64);
    outlen = 0;
    utils_hex_to_bin(prevblock_hex, bheaderprev.prev_block, 64, &outlen);
    btc_free(prevblock_hex);

    merkleroot_hex_o = "3696737d03075235b3874ed2ec6e93555e3259f818f53f3c241a2ae74f18ab07";
    merkleroot_hex = btc_malloc(strlen(merkleroot_hex_o)+1);
    memcpy(merkleroot_hex, merkleroot_hex_o, strlen(merkleroot_hex_o));
    utils_reverse_hex(merkleroot_hex, 64);
    outlen = 0;
    utils_hex_to_bin(merkleroot_hex, bheaderprev.merkle_root, 64, &outlen);
    btc_free(merkleroot_hex);

    /* compare blockheaderhex */
    cstring *blockheader_ser = cstr_new_sz(256);
    btc_block_header_serialize(blockheader_ser, &bheader);
    char *blockheader_h427928 = "00000020e5c87d63e62f2a82837768e2407e574d5b300c6f43ad9800000000000000000076b79ec639a4469b092b0f076aa2483bfc8a1e3f0fa9a2c23d2ab72b150e69d42c30c95708fb0418a3668daf";
    char *blockheader_hash_h427928 = "00000000000000000127d7e285f5d9ad281d236353d73a176a56f7dab499b5b6";

    char headercheck[1024];
    utils_bin_to_hex((unsigned char *)blockheader_ser->str, blockheader_ser->len, headercheck);
    u_assert_str_eq(headercheck, blockheader_h427928);

    uint256 checkhash;
    btc_block_header_hash(&bheader, (uint8_t *)&checkhash);
    char hashhex[sizeof(checkhash)*2];
    utils_bin_to_hex(checkhash, sizeof(checkhash), hashhex);
    utils_reverse_hex(hashhex, strlen(hashhex));
    u_assert_str_eq(blockheader_hash_h427928, hashhex);

    struct const_buffer buf;
    buf.p = blockheader_ser->str;
    buf.len = blockheader_ser->len;
    btc_block_header_deserialize(&bheadercheck, &buf);
    u_assert_mem_eq(&bheader, &bheadercheck, sizeof(bheadercheck));
    cstr_free(blockheader_ser, true);


    btc_block_header_hash(&bheaderprev, (uint8_t *)&checkhash);
    u_assert_mem_eq(&checkhash, &bheader.prev_block, sizeof(checkhash));
}
