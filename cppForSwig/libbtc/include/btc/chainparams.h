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

#ifndef __LIBBTC_CHAINPARAMS_H__
#define __LIBBTC_CHAINPARAMS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <stdint.h>
#include <sys/types.h>

typedef struct btc_dns_seed_ {
    char domain[256];
} btc_dns_seed;

typedef struct btc_chainparams_ {
    char chainname[32];
    uint8_t b58prefix_pubkey_address;
    uint8_t b58prefix_script_address;
    const char bech32_hrp[5];
    uint8_t b58prefix_secret_address; //!private key
    uint32_t b58prefix_bip32_privkey;
    uint32_t b58prefix_bip32_pubkey;
    const unsigned char netmagic[4];
    uint256 genesisblockhash;
    int default_port;
    btc_dns_seed dnsseeds[8];
} btc_chainparams;

typedef struct btc_checkpoint_ {
    uint32_t height;
    const char* hash;
    uint32_t timestamp;
    uint32_t target;
} btc_checkpoint;

extern const btc_chainparams btc_chainparams_main;
extern const btc_chainparams btc_chainparams_test;
extern const btc_chainparams btc_chainparams_regtest;

// the mainnet checkpoins, needs a fix size
extern const btc_checkpoint btc_mainnet_checkpoint_array[21];

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_CHAINPARAMS_H__
