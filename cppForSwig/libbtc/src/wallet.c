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

#include <btc/base58.h>
#include <btc/blockchain.h>
#include <btc/serialize.h>
#include <btc/wallet.h>
#include <btc/utils.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <search.h>

#define COINBASE_MATURITY 100

uint8_t WALLET_DB_REC_TYPE_MASTERKEY = 0;
uint8_t WALLET_DB_REC_TYPE_PUBKEYCACHE = 1;
uint8_t WALLET_DB_REC_TYPE_TX = 2;

static const unsigned char file_hdr_magic[4] = {0xA8, 0xF0, 0x11, 0xC5}; /* header magic */
static const uint32_t current_version = 1;

static const char* hdkey_key = "hdkey";
static const char* hdmasterkey_key = "mstkey";
static const char* tx_key = "tx";


/* ====================== */
/* compare btree callback */
/* ====================== */
int btc_wallet_hdnode_compare(const void *l, const void *r)
{
    const btc_wallet_hdnode *lm = l;
    const btc_wallet_hdnode *lr = r;

    uint8_t *pubkeyA = (uint8_t *)lm->pubkeyhash;
    uint8_t *pubkeyB = (uint8_t *)lr->pubkeyhash;

    /* byte per byte compare */
    /* TODO: switch to memcmp */
    for (unsigned int i = 0; i < sizeof(uint160); i++) {
        uint8_t iA = pubkeyA[i];
        uint8_t iB = pubkeyB[i];
        if (iA > iB)
            return -1;
        else if (iA < iB)
            return 1;
    }

    return 0;
}

int btc_wtx_compare(const void *l, const void *r)
{
    const btc_wtx *lm = l;
    const btc_wtx *lr = r;

    uint8_t *hashA = (uint8_t *)lm->tx_hash_cache;
    uint8_t *hashB = (uint8_t *)lr->tx_hash_cache;

    /* byte per byte compare */
    for (unsigned int i = 0; i < sizeof(uint256); i++) {
        uint8_t iA = hashA[i];
        uint8_t iB = hashB[i];
        if (iA > iB)
            return -1;
        else if (iA < iB)
            return 1;
    }

    return 0;
}


/*
 ==========================================================
 WALLET TRANSACTION (WTX) FUNCTIONS
 ==========================================================
*/

btc_wtx* btc_wallet_wtx_new()
{
    btc_wtx* wtx;
    wtx = btc_calloc(1, sizeof(*wtx));
    wtx->height = 0;
    wtx->tx = btc_tx_new();

    return wtx;
}

btc_wtx* btc_wallet_wtx_copy(btc_wtx* wtx)
{
    btc_wtx* wtx_copy;
    wtx_copy = btc_wallet_wtx_new();
    btc_tx_copy(wtx_copy->tx, wtx->tx);

    return wtx_copy;
}

void btc_wallet_wtx_free(btc_wtx* wtx)
{
    btc_tx_free(wtx->tx);
    btc_free(wtx);
}

void btc_wallet_wtx_serialize(cstring* s, const btc_wtx* wtx)
{
    ser_u32(s, wtx->height);
    ser_u256(s, wtx->tx_hash_cache);
    btc_tx_serialize(s, wtx->tx, true);
}

btc_bool btc_wallet_wtx_deserialize(btc_wtx* wtx, struct const_buffer* buf)
{
    deser_u32(&wtx->height, buf);
    deser_u256(wtx->tx_hash_cache, buf);
    return btc_tx_deserialize(buf->p, buf->len, wtx->tx, NULL, true);
}

/*
 ==========================================================
 WALLET HDNODE (WALLET_HDNODE) FUNCTIONS
 ==========================================================
*/

btc_wallet_hdnode* btc_wallet_hdnode_new()
{
    btc_wallet_hdnode* whdnode;
    whdnode = btc_calloc(1, sizeof(*whdnode));
    whdnode->hdnode = btc_hdnode_new();

    return whdnode;
}
void btc_wallet_hdnode_free(btc_wallet_hdnode* whdnode)
{
    btc_hdnode_free(whdnode->hdnode);
    btc_free(whdnode);
}

void btc_wallet_hdnode_serialize(cstring* s, const btc_chainparams *params, const btc_wallet_hdnode* whdnode)
{
    ser_bytes(s, whdnode->pubkeyhash, sizeof(uint160));
    char strbuf[196];
    btc_hdnode_serialize_private(whdnode->hdnode, params, strbuf, sizeof(strbuf));
    ser_str(s, strbuf, sizeof(strbuf));
}

btc_bool btc_wallet_hdnode_deserialize(btc_wallet_hdnode* whdnode, const btc_chainparams *params, struct const_buffer* buf) {
    deser_bytes(&whdnode->pubkeyhash, buf, sizeof(uint160));
    char strbuf[196];
    if (!deser_str(strbuf, buf, sizeof(strbuf))) return false;
    if (!btc_hdnode_deserialize(strbuf, params, whdnode->hdnode)) return false;
    return true;
}

/*
 ==========================================================
 WALLET OUTPUT (prev wtx + n) FUNCTIONS
 ==========================================================
 */

btc_output* btc_wallet_output_new()
{
    btc_output* output;
    output = btc_calloc(1, sizeof(*output));
    output->i = 0;
    output->wtx = btc_wallet_wtx_new();

    return output;
}

void btc_wallet_output_free(btc_output* output)
{
    btc_wallet_wtx_free(output->wtx);
    btc_free(output);
}

/*
 ==========================================================
 WALLET CORE FUNCTIONS
 ==========================================================
 */
btc_wallet* btc_wallet_new(const btc_chainparams *params)
{
    btc_wallet* wallet;
    wallet = btc_calloc(1, sizeof(*wallet));
    wallet->masterkey = NULL;
    wallet->chain = params;
    wallet->spends = vector_new(10, free);

    wallet->wtxes_rbtree = 0;
    wallet->hdkeys_rbtree = 0;
    return wallet;
}

void btc_wallet_free(btc_wallet* wallet)
{
    if (!wallet)
        return;

    if (wallet->dbfile) {
        fclose(wallet->dbfile);
        wallet->dbfile = NULL;
    }

    if (wallet->spends) {
        vector_free(wallet->spends, true);
        wallet->spends = NULL;
    }

    if (wallet->masterkey)
        btc_free(wallet->masterkey);

    btc_btree_tdestroy(wallet->wtxes_rbtree, btc_free);
    btc_btree_tdestroy(wallet->hdkeys_rbtree, btc_free);

    btc_free(wallet);
}

//void btc_wallet_logdb_append_cb(void* ctx, logdb_bool load_phase, logdb_record* rec)
//{
//    btc_wallet* wallet = (btc_wallet*)ctx;
//    if (load_phase) {
//        if (wallet->masterkey == NULL && rec->mode == RECORD_TYPE_WRITE && rec->key->len > strlen(hdmasterkey_key) && memcmp(rec->key->str, hdmasterkey_key, strlen(hdmasterkey_key)) == 0) {
//            wallet->masterkey = btc_hdnode_new();
//            btc_hdnode_deserialize(rec->value->str, wallet->chain, wallet->masterkey);
//        }
//        if (rec->key->len == strlen(hdkey_key) + sizeof(uint160) && memcmp(rec->key->str, hdkey_key, strlen(hdkey_key)) == 0) {
//            btc_hdnode* hdnode = btc_hdnode_new();
//            btc_hdnode_deserialize(rec->value->str, wallet->chain, hdnode);

//            /* rip out the hash from the record key (avoid re-SHA256) */
//            cstring* keyhash160 = cstr_new_buf(rec->key->str + strlen(hdkey_key), sizeof(uint160));

//            /* add hdnode to the rbtree */
//            RBTreeInsert(wallet->hdkeys_rbtree, keyhash160, hdnode);

//            if (hdnode->child_num + 1 > wallet->next_childindex)
//                wallet->next_childindex = hdnode->child_num + 1;
//        }

//        if (rec->key->len == strlen(tx_key) + SHA256_DIGEST_LENGTH && memcmp(rec->key->str, tx_key, strlen(tx_key)) == 0) {
//            btc_wtx* wtx = btc_wallet_wtx_new();
//            struct const_buffer buf = {rec->value->str, rec->value->len};

//            /* deserialize transaction */
//            btc_wallet_wtx_deserialize(wtx, &buf);

//            /* rip out the hash from the record key (avoid re-SHA256) */
//            cstring* wtxhash = cstr_new_buf(rec->key->str + strlen(tx_key), SHA256_DIGEST_LENGTH);

//            /* add wtx to the rbtree */
//            RBTreeInsert(wallet->wtxes_rbtree, wtxhash, wtx);

//            /* add to spends */
//            btc_wallet_add_to_spent(wallet, wtx);
//        }
//    }
//}

btc_bool btc_wallet_load(btc_wallet* wallet, const char* file_path, int *error, btc_bool *created)
{
    (void)(error);
    if (!wallet)
        return false;

    struct stat buffer;
    *created = true;
    if (stat(file_path, &buffer) == 0)
        *created = false;

    wallet->dbfile = fopen(file_path, *created ? "a+b" : "r+b");

    if (*created) {
        // write file-header-magic
        if (fwrite(file_hdr_magic, 4, 1, wallet->dbfile ) != 1 ) return false;

        // write version
        uint32_t v = htole32(current_version);
        if (fwrite(&v, sizeof(v), 1, wallet->dbfile ) != 1) return false;

        // write genesis
        if (fwrite(wallet->chain->genesisblockhash, sizeof(uint256), 1, wallet->dbfile ) != 1) return false;

        btc_file_commit(wallet->dbfile);
    }
    else {
        // check file-header-magic, version and genesis
        uint8_t buf[sizeof(file_hdr_magic)+sizeof(current_version)+sizeof(uint256)];
        if ( (uint32_t)buffer.st_size < (uint32_t)(sizeof(buf)) ||
             fread(buf, sizeof(buf), 1, wallet->dbfile ) != 1 ||
             memcmp(buf, file_hdr_magic, sizeof(file_hdr_magic))
            )
        {
            fprintf(stderr, "Wallet file: error reading database file\n");
            return false;
        }
        if (le32toh(*(buf+sizeof(file_hdr_magic))) > current_version) {
            fprintf(stderr, "Wallet file: unsupported file version\n");
            return false;
        }
        if (memcmp(buf+sizeof(file_hdr_magic)+sizeof(current_version), wallet->chain->genesisblockhash, sizeof(uint256)) != 0) {
            fprintf(stderr, "Wallet file: different network\n");
            return false;
        }
        // read

        while (!feof(wallet->dbfile))
        {
            uint8_t rectype;
            if (fread(&rectype, 1, 1, wallet->dbfile ) != 1) {
                // no more record, break
                break;
            }

            if (rectype == WALLET_DB_REC_TYPE_MASTERKEY) {
                uint32_t len;
                char strbuf[196];
                if (!deser_varlen_from_file(&len, wallet->dbfile)) return false;
                if (len > sizeof(strbuf)) { return false; }
                if (fread(strbuf, len, 1, wallet->dbfile ) != 1) return false;
                size_t test = strlen(strbuf);
                wallet->masterkey = btc_hdnode_new();
                printf("xpriv: %s\n", strbuf);
                btc_hdnode_deserialize(strbuf, wallet->chain, wallet->masterkey );
                int i = 0;
            }

            if (rectype == WALLET_DB_REC_TYPE_PUBKEYCACHE) {
                uint32_t len;

                btc_wallet_hdnode *whdnode = btc_wallet_hdnode_new();
                if (fread(whdnode->pubkeyhash, sizeof(uint160), 1, wallet->dbfile ) != 1) {
                    btc_wallet_hdnode_free(whdnode);
                    return false;
                }

                // read the varint for the stringlength
                char strbuf[1024];
                if (!deser_varlen_from_file(&len, wallet->dbfile)) {
                    btc_wallet_hdnode_free(whdnode);
                    return false;
                }
                if (len > sizeof(strbuf)) { return false; }
                if (fread(strbuf, len, 1, wallet->dbfile ) != 1) {
                    btc_wallet_hdnode_free(whdnode);
                    return false;
                }
                // deserialize the hdnode
                if (!btc_hdnode_deserialize(strbuf, wallet->chain, whdnode->hdnode)) {
                    btc_wallet_hdnode_free(whdnode);
                    return false;
                }

                // add the node to the binary tree
                btc_wallet_hdnode* checknode = tsearch(whdnode, &wallet->hdkeys_rbtree, btc_wallet_hdnode_compare);

            }
        }
    }

    return true;
}

btc_bool btc_wallet_flush(btc_wallet* wallet)
{
    btc_file_commit(wallet->dbfile);
    return true;
}

void btc_wallet_set_master_key_copy(btc_wallet* wallet, btc_hdnode* masterkey)
{
    if (!masterkey)
        return;

    if (wallet->masterkey != NULL) {
        //changing the master key should not be done,...
        //anyways, we are going to accept that at this point
        //consuming application needs to take care about that
        btc_hdnode_free(wallet->masterkey);
        wallet->masterkey = NULL;
    }
    wallet->masterkey = btc_hdnode_copy(masterkey);

    cstring* record = cstr_new_sz(256);
    ser_bytes(record, &WALLET_DB_REC_TYPE_MASTERKEY, 1);
    char strbuf[196];
    btc_hdnode_serialize_private(wallet->masterkey, wallet->chain, strbuf, sizeof(strbuf));
    printf("xpriv: %s\n", strbuf);
    ser_str(record, strbuf, sizeof(strbuf));

    if ( fwrite(record->str, record->len, 1, wallet->dbfile) != 1 ) {
        fprintf(stderr, "Writing master private key record failed\n");
    }
    cstr_free(record, true);

    btc_file_commit(wallet->dbfile);
}

btc_wallet_hdnode* btc_wallet_next_key(btc_wallet* wallet)
{
    if (!wallet || !wallet->masterkey)
        return NULL;

    //for now, only m/k is possible
    btc_wallet_hdnode *whdnode = btc_wallet_hdnode_new();
    btc_hdnode_free(whdnode->hdnode);
    whdnode->hdnode = btc_hdnode_copy(wallet->masterkey);
    btc_hdnode_private_ckd(whdnode->hdnode, wallet->next_childindex);
    btc_hdnode_get_hash160(whdnode->hdnode, whdnode->pubkeyhash);

    //add it to the binary tree
    // tree manages memory
    btc_wallet_hdnode* checknode = tsearch(whdnode, &wallet->hdkeys_rbtree, btc_wallet_hdnode_compare);

    //serialize and store node
    cstring* record = cstr_new_sz(256);
    ser_bytes(record, &WALLET_DB_REC_TYPE_PUBKEYCACHE, 1);
    btc_wallet_hdnode_serialize(record, wallet->chain, whdnode);

    if (fwrite(record->str, record->len, 1, wallet->dbfile) != 1) {
        fprintf(stderr, "Writing childkey failed\n");
    }
    cstr_free(record, true);

    btc_file_commit(wallet->dbfile);

    //increase the in-memory counter (cache)
    wallet->next_childindex++;

    return whdnode;
}

void btc_wallet_get_addresses(btc_wallet* wallet, vector* addr_out)
{
    (void)(wallet);
    (void)(addr_out);
//    rb_red_blk_node* hdkey_rbtree_node;

//    if (!wallet)
//        return;

//    while ((hdkey_rbtree_node = rbtree_enumerate_next(wallet->hdkeys_rbtree))) {
//        cstring* key = hdkey_rbtree_node->key;
//        uint8_t hash160[sizeof(uint160)+1];
//        hash160[0] = wallet->chain->b58prefix_pubkey_address;
//        memcpy(hash160 + 1, key->str, sizeof(uint160));

//        size_t addrsize = 98;
//        char* addr = btc_calloc(1, addrsize);
//        btc_base58_encode_check(hash160, sizeof(uint160)+1, addr, addrsize);
//        vector_add(addr_out, addr);
//    }
}

btc_wallet_hdnode* btc_wallet_find_hdnode_byaddr(btc_wallet* wallet, const char* search_addr)
{
    if (!wallet || !search_addr)
        return NULL;

    uint8_t hashdata[strlen(search_addr)];
    memset(hashdata, 0, sizeof(uint160));
    int outlen = btc_base58_decode_check(search_addr, hashdata, strlen(search_addr));
    if (outlen == 0) {
        return NULL;
    }

    btc_wallet_hdnode* whdnode_search;
    whdnode_search = btc_calloc(1, sizeof(*whdnode_search));
    memcpy(whdnode_search->pubkeyhash, hashdata+1, sizeof(uint160));

    btc_wallet_hdnode *needle = tfind(whdnode_search, &wallet->hdkeys_rbtree, btc_wallet_hdnode_compare); /* read */
    if (needle) {
        needle = *(btc_wallet_hdnode **)needle;
    }
    btc_free(whdnode_search);

    return needle;
}

btc_bool btc_wallet_add_wtx_move(btc_wallet* wallet, btc_wtx* wtx)
{
    if (!wallet || !wtx)
        return false;

    cstring* record = cstr_new_sz(1024);
    ser_bytes(record, &WALLET_DB_REC_TYPE_TX, 1);
    btc_wallet_wtx_serialize(record, wtx);

    if (fwrite(record->str, record->len, 1, wallet->dbfile) ) {
        fprintf(stderr, "Writing master private key record failed\n");
    }
    cstr_free(record, true);

    //add to spends
    btc_wallet_add_to_spent(wallet, wtx);

    //add it to the binary tree
    btc_wtx* checkwtx = tsearch(wtx, &wallet->wtxes_rbtree, btc_wtx_compare);
    if (checkwtx) {
        // remove existing wtx
        checkwtx = *(btc_wtx **)checkwtx;
        tdelete(checkwtx, &wallet->wtxes_rbtree, btc_wtx_compare);
        btc_wallet_wtx_free(checkwtx);

        // insert again
        btc_wtx* checkwtx = tsearch(wtx, &wallet->wtxes_rbtree, btc_wtx_compare);
    }


    return true;
}

btc_bool btc_wallet_have_key(btc_wallet* wallet, uint160 hash160)
{
    if (!wallet)
        return false;

    btc_wallet_hdnode* whdnode_search;
    whdnode_search = btc_calloc(1, sizeof(*whdnode_search));
    memcpy(whdnode_search->pubkeyhash, hash160, sizeof(uint160));

    btc_wallet_hdnode *needle = tfind(whdnode_search, &wallet->hdkeys_rbtree, btc_wallet_hdnode_compare); /* read */
    if (needle) {
        needle = *(btc_wallet_hdnode **)needle;
    }
    btc_free(whdnode_search);

    return (needle != NULL);
}

int64_t btc_wallet_get_balance(btc_wallet* wallet)
{
    int64_t credit = 0;

    if (!wallet)
        return false;

//    // enumerate over the rbtree, calculate balance
//    while ((hdkey_rbtree_node = rbtree_enumerate_next(wallet->wtxes_rbtree))) {
//        btc_wtx* wtx = hdkey_rbtree_node->info;
//        credit += btc_wallet_wtx_get_credit(wallet, wtx);
//    }

    return credit;
}

int64_t btc_wallet_wtx_get_credit(btc_wallet* wallet, btc_wtx* wtx)
{
    int64_t credit = 0;

    if (btc_tx_is_coinbase(wtx->tx) &&
        (wallet->bestblockheight < COINBASE_MATURITY || wtx->height > wallet->bestblockheight - COINBASE_MATURITY))
        return credit;

    uint256 hash;
    btc_tx_hash(wtx->tx, hash);
    unsigned int i = 0;
    if (wtx->tx->vout) {
        for (i = 0; i < wtx->tx->vout->len; i++) {
            btc_tx_out* tx_out;
            tx_out = vector_idx(wtx->tx->vout, i);

            if (!btc_wallet_is_spent(wallet, hash, i)) {
                if (btc_wallet_txout_is_mine(wallet, tx_out))
                    credit += tx_out->value;
            }
        }
    }
    return credit;
}

btc_bool btc_wallet_txout_is_mine(btc_wallet* wallet, btc_tx_out* tx_out)
{
    if (!wallet || !tx_out) return false;

    btc_bool ismine = false;

    vector* vec = vector_new(16, free);
    enum btc_tx_out_type type2 = btc_script_classify(tx_out->script_pubkey, vec);

    //TODO: Multisig, etc.
    if (type2 == BTC_TX_PUBKEYHASH) {
        //TODO: find a better format for vector elements (not a pure pointer)
        uint8_t* hash160 = vector_idx(vec, 0);
        if (btc_wallet_have_key(wallet, hash160))
            ismine = true;
    }

    vector_free(vec, true);

    return ismine;
}

btc_bool btc_wallet_is_mine(btc_wallet* wallet, const btc_tx *tx)
{
    if (!wallet || !tx) return false;
    if (tx->vout) {
        for (unsigned int i = 0; i < tx->vout->len; i++) {
            btc_tx_out* tx_out = vector_idx(tx->vout, i);
            if (tx_out && btc_wallet_txout_is_mine(wallet, tx_out)) {
                return true;
            }
        }
    }
    return false;
}

int64_t btc_wallet_get_debit_txi(btc_wallet *wallet, const btc_tx_in *txin) {
    if (!wallet || !txin) return 0;

    btc_wtx wtx;
    memcpy(wtx.tx_hash_cache, txin->prevout.hash, sizeof(wtx.tx_hash_cache));

    btc_wtx* checkwtx = tfind(&wtx, &wallet->wtxes_rbtree, btc_wtx_compare);
    if (checkwtx) {
        // remove existing wtx
        checkwtx = *(btc_wtx **)checkwtx;
        //todo get debig
    }

    return 0;
}

int64_t btc_wallet_get_debit_tx(btc_wallet *wallet, const btc_tx *tx) {
    int64_t debit = 0;
    if (tx->vin) {
        for (unsigned int i = 0; i < tx->vin->len; i++) {
            btc_tx_in* tx_in= vector_idx(tx->vin, i);
            if (tx_in) {
                debit += btc_wallet_get_debit_txi(wallet, tx_in);
            }
        }
    }
    return debit;
}

btc_bool btc_wallet_is_from_me(btc_wallet *wallet, const btc_tx *tx)
{
    return (btc_wallet_get_debit_tx(wallet, tx) > 0);
}

void btc_wallet_add_to_spent(btc_wallet* wallet, btc_wtx* wtx) {
    if (!wallet || !wtx)
        return;

    if (btc_tx_is_coinbase(wtx->tx))
        return;

    unsigned int i = 0;
    if (wtx->tx->vin) {
        for (i = 0; i < wtx->tx->vin->len; i++) {
            btc_tx_in* tx_in = vector_idx(wtx->tx->vin, i);

            //add to spends
            btc_tx_outpoint* outpoint = btc_calloc(1, sizeof(btc_tx_outpoint));
            memcpy(outpoint, &tx_in->prevout, sizeof(btc_tx_outpoint));
            vector_add(wallet->spends, outpoint);
        }
    }
}

btc_bool btc_wallet_is_spent(btc_wallet* wallet, uint256 hash, uint32_t n)
{
    if (!wallet)
        return false;

    unsigned int i = 0;
    for (i = wallet->spends->len; i > 0; i--) {
        btc_tx_outpoint* outpoint = vector_idx(wallet->spends, i - 1);
        if (memcmp(outpoint->hash, hash, BTC_HASH_LENGTH) == 0 && n == outpoint->n)
            return true;
    }
    return false;
}

btc_bool btc_wallet_get_unspent(btc_wallet* wallet, vector* unspents)
{
    (void)(wallet);
    (void)(unspents);
    return true;
//    rb_red_blk_node* hdkey_rbtree_node;

//    if (!wallet)
//        return false;

//    while ((hdkey_rbtree_node = rbtree_enumerate_next(wallet->wtxes_rbtree))) {
//        btc_wtx* wtx = hdkey_rbtree_node->info;
//        cstring* key = hdkey_rbtree_node->key;
//        uint8_t* hash = (uint8_t*)key->str;

//        unsigned int i = 0;
//        if (wtx->tx->vout) {
//            for (i = 0; i < wtx->tx->vout->len; i++) {
//                btc_tx_out* tx_out;
//                tx_out = vector_idx(wtx->tx->vout, i);

//                if (!btc_wallet_is_spent(wallet, hash, i)) {
//                    if (btc_wallet_txout_is_mine(wallet, tx_out)) {
//                        btc_output* output = btc_wallet_output_new();
//                        btc_wallet_wtx_free(output->wtx);
//                        output->wtx = btc_wallet_wtx_copy(wtx);
//                        output->i = i;
//                        vector_add(unspents, output);
//                    }
//                }
//            }
//        }
//    }

//    return true;
}

void btc_wallet_check_transaction(void *ctx, btc_tx *tx, unsigned int pos, btc_blockindex *pindex) {
    (void)(pos);
    (void)(pindex);
    btc_wallet *wallet = (btc_wallet *)ctx;
    if (btc_wallet_is_mine(wallet, tx) || btc_wallet_is_from_me(wallet, tx)) {
        int i = 0;
        printf("\nFound relevant transaction!\n");
    }
}
