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

#ifndef __LIBBTC_HEADERSDB_FILE_H__
#define __LIBBTC_HEADERSDB_FILE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"
#include "blockchain.h"
#include "buffer.h"
#include "chainparams.h"

#include "headersdb.h"

#include <stdio.h>

/* filebased headers database (including binary tree option for fast access)
*/
typedef struct btc_headers_db_
{
    FILE *headers_tree_file;
    btc_bool read_write_file;

    void *tree_root;
    btc_bool use_binary_tree;

    unsigned int max_hdr_in_mem;
    btc_blockindex genesis;
    btc_blockindex *chaintip;
    btc_blockindex *chainbottom;
} btc_headers_db;

btc_headers_db *btc_headers_db_new(const btc_chainparams* chainparams, btc_bool inmem_only);
void btc_headers_db_free(btc_headers_db *db);

btc_bool btc_headers_db_load(btc_headers_db* db, const char *filename);
btc_blockindex * btc_headers_db_connect_hdr(btc_headers_db* db, struct const_buffer *buf, btc_bool load_process, btc_bool *connected);

void btc_headers_db_fill_block_locator(btc_headers_db* db, vector *blocklocators);

btc_blockindex * btc_headersdb_find(btc_headers_db* db, uint256 hash);
btc_blockindex * btc_headersdb_getchaintip(btc_headers_db* db);
btc_bool btc_headersdb_disconnect_tip(btc_headers_db* db);

btc_bool btc_headersdb_has_checkpoint_start(btc_headers_db* db);
void btc_headersdb_set_checkpoint_start(btc_headers_db* db, uint256 hash, uint32_t height);


// interface function pointer bindings
static const btc_headers_db_interface btc_headers_db_interface_file = {
    (void* (*)(const btc_chainparams*, btc_bool))btc_headers_db_new,
    (void (*)(void *))btc_headers_db_free,
    (btc_bool (*)(void *, const char *))btc_headers_db_load,
    (void (*)(void* , vector *))btc_headers_db_fill_block_locator,
    (btc_blockindex *(*)(void* , struct const_buffer *, btc_bool , btc_bool *))btc_headers_db_connect_hdr,

    (btc_blockindex* (*)(void *))btc_headersdb_getchaintip,
    (btc_bool (*)(void *))btc_headersdb_disconnect_tip,

    (btc_bool (*)(void *))btc_headersdb_has_checkpoint_start,
    (void (*)(void *, uint256, uint32_t))btc_headersdb_set_checkpoint_start
};

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_HEADERSDB_FILE_H__
