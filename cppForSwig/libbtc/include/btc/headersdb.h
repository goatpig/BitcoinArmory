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

#ifndef __LIBBTC_HEADERSDB_H__
#define __LIBBTC_HEADERSDB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"
#include "blockchain.h"
#include "buffer.h"
#include "chainparams.h"

#include <stdio.h>
#include <logdb/logdb.h>
#include <logdb/logdb_rec.h>

/* headers database interface, flexible function pointers in
   order to support multiple backends
*/
typedef struct btc_headers_db_interface_
{
    /* init database handler */
    void* (*init)(const btc_chainparams* chainparams, btc_bool inmem_only);

    /* deallocs database handler */
    void (*free)(void *db);

    /* loads database from filename */
    btc_bool (*load)(void *db, const char *filename);

    /* fill in blocklocator up to the tip */
    void (*fill_blocklocator_tip)(void* db, vector *blocklocators);

    /* connect (append) a header */
    btc_blockindex *(*connect_hdr)(void* db, struct const_buffer *buf, btc_bool load_process, btc_bool *connected);

    /* get the chain tip */
    btc_blockindex* (*getchaintip)(void *db);

    /* disconnect the tip and return true if it was possible */
    btc_bool (*disconnect_tip)(void *db);

    /* check if we are using a pruned header db staring at a checkpoint */
    btc_bool (*has_checkpoint_start)(void *db);

    /* set that we are using a checkpoint as basepoint at given height with given hash */
    void (*set_checkpoint_start)(void *db, uint256 hash, uint32_t height);
} btc_headers_db_interface;


#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_HEADERSDB_H__
