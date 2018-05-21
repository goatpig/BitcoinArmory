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

#ifndef __LIBBTC_WALLET_H__
#define __LIBBTC_WALLET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"

#include <btc/blockchain.h>

#include "bip32.h"
#include "buffer.h"
#include "tx.h"

#include <stddef.h>
#include <stdint.h>

/** single key/value record */
typedef struct btc_wallet {
    FILE *dbfile;
    btc_hdnode* masterkey;
    uint32_t next_childindex; //cached next child index
    const btc_chainparams* chain;
    uint32_t bestblockheight;
    vector* spends;

    /* use binary trees for in-memory mapping for wtxs, keys */
    void* wtxes_rbtree;
    void* hdkeys_rbtree;
} btc_wallet;

typedef struct btc_wtx_ {
    uint256 tx_hash_cache;
    uint32_t height;
    btc_tx* tx;
} btc_wtx;

typedef struct btc_wallet_hdnode_ {
    uint160 pubkeyhash;
    btc_hdnode *hdnode;
} btc_wallet_hdnode;

typedef struct btc_output_ {
    uint32_t i;
    btc_wtx* wtx;
} btc_output;

/** wallet transaction (wtx) functions */
LIBBTC_API btc_wtx* btc_wallet_wtx_new();
LIBBTC_API void btc_wallet_wtx_free(btc_wtx* wtx);
LIBBTC_API void btc_wallet_wtx_serialize(cstring* s, const btc_wtx* wtx);
LIBBTC_API btc_bool btc_wallet_wtx_deserialize(btc_wtx* wtx, struct const_buffer* buf);
/** ------------------------------------ */

/** wallet hdnode (wallet_hdnode) functions */
LIBBTC_API btc_wallet_hdnode* btc_wallet_hdnode_new();
LIBBTC_API void btc_wallet_hdnode_free(btc_wallet_hdnode* whdnode);
LIBBTC_API void btc_wallet_hdnode_serialize(cstring* s, const btc_chainparams *params, const btc_wallet_hdnode* whdnode);
LIBBTC_API btc_bool btc_wallet_hdnode_deserialize(btc_wallet_hdnode* whdnode, const btc_chainparams *params, struct const_buffer* buf);
/** ------------------------------------ */

/** wallet outputs (prev wtx + n) functions */
LIBBTC_API btc_output* btc_wallet_output_new();
LIBBTC_API void btc_wallet_output_free(btc_output* output);
/** ------------------------------------ */

LIBBTC_API btc_wallet* btc_wallet_new(const btc_chainparams *params);
LIBBTC_API void btc_wallet_free(btc_wallet* wallet);

/** load the wallet, sets masterkey, sets next_childindex */
LIBBTC_API btc_bool btc_wallet_load(btc_wallet* wallet, const char* file_path, int *error, btc_bool *created);

/** writes the wallet state to disk */
LIBBTC_API btc_bool btc_wallet_flush(btc_wallet* wallet);

/** set the master key of new created wallet
 consuming app needs to ensure that we don't override exiting masterkeys */
LIBBTC_API void btc_wallet_set_master_key_copy(btc_wallet* wallet, btc_hdnode* masterkey);

/** derives the next child hdnode (memory is owned by the wallet) */
LIBBTC_API btc_wallet_hdnode* btc_wallet_next_key(btc_wallet* wallet);

/** writes all available addresses (P2PKH) to the addr_out vector */
LIBBTC_API void btc_wallet_get_addresses(btc_wallet* wallet, vector* addr_out);

/** searches after a hdnode by given P2PKH (base58(hash160)) address */
LIBBTC_API btc_wallet_hdnode* btc_wallet_find_hdnode_byaddr(btc_wallet* wallet, const char* search_addr);

/** adds transaction to the wallet (hands over memory management) */
LIBBTC_API btc_bool btc_wallet_add_wtx_move(btc_wallet* wallet, btc_wtx* wtx);

/** looks if a key with the hash160 (SHA256/RIPEMD) exists */
LIBBTC_API btc_bool btc_wallet_have_key(btc_wallet* wallet, uint160 hash160);

/** gets credit from given transaction */
LIBBTC_API int64_t btc_wallet_get_balance(btc_wallet* wallet);

/** gets credit from given transaction */
LIBBTC_API int64_t btc_wallet_wtx_get_credit(btc_wallet* wallet, btc_wtx* wtx);

/** checks if a transaction outpoint is owned by the wallet */
LIBBTC_API btc_bool btc_wallet_txout_is_mine(btc_wallet* wallet, btc_tx_out* tx_out);

/** checks if a transaction outpoint is owned by the wallet */
LIBBTC_API void btc_wallet_add_to_spent(btc_wallet* wallet, btc_wtx* wtx);
LIBBTC_API btc_bool btc_wallet_is_spent(btc_wallet* wallet, uint256 hash, uint32_t n);
LIBBTC_API btc_bool btc_wallet_get_unspent(btc_wallet* wallet, vector* unspents);

/** checks a transaction or relevance to the wallet */
LIBBTC_API void btc_wallet_check_transaction(void *ctx, btc_tx *tx, unsigned int pos, btc_blockindex *pindex);

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_WALLET_H__
