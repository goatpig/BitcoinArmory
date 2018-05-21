/*

 The MIT License (MIT)

 Copyright (c) 2017 Jonas Schnelli

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

#include <btc/headersdb_file.h>
#include <btc/block.h>
#include <btc/serialize.h>
#include <btc/utils.h>

#include <sys/stat.h>

#include <search.h>

static const unsigned char file_hdr_magic[4] = {0xA8, 0xF0, 0x11, 0xC5}; /* header magic */
static const uint32_t current_version = 1;

int btc_header_compare(const void *l, const void *r)
{
    const btc_blockindex *lm = l;
    const btc_blockindex *lr = r;

    uint8_t *hashA = (uint8_t *)lm->hash;
    uint8_t *hashB = (uint8_t *)lr->hash;

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

btc_headers_db* btc_headers_db_new(const btc_chainparams* chainparams, btc_bool inmem_only) {
    btc_headers_db* db;
    db = btc_calloc(1, sizeof(*db));

    db->read_write_file = !inmem_only;
    db->use_binary_tree = true;
    db->max_hdr_in_mem = 144;

    db->genesis.height = 0;
    db->genesis.prev = NULL;
    memcpy(db->genesis.hash, chainparams->genesisblockhash, BTC_HASH_LENGTH);
    db->chaintip = &db->genesis;
    db->chainbottom = &db->genesis;

    if (db->use_binary_tree) {
        db->tree_root = 0;
    }

    return db;
}

void btc_headers_db_free(btc_headers_db* db) {

    if (!db)
        return;

    if (db->headers_tree_file)
    {
        fclose(db->headers_tree_file);
        db->headers_tree_file = NULL;
    }

    if (db->tree_root) {
        btc_btree_tdestroy(db->tree_root, btc_free);
        db->tree_root = NULL;
    }

    btc_free(db);
}

btc_bool btc_headers_db_load(btc_headers_db* db, const char *file_path) {
    if (!db->read_write_file) {
        /* stop at this point if we do inmem only */
        return 1;
    }

    char *file_path_local = (char *)file_path;
    cstring *path_ret = cstr_new_sz(1024);
    if (!file_path)
    {
        btc_get_default_datadir(path_ret);
        char *filename = "/headers.db";
        cstr_append_buf(path_ret, filename, strlen(filename));
        cstr_append_c(path_ret, 0);
        file_path_local = path_ret->str;
    }

    struct stat buffer;
    btc_bool create = true;
    if (stat(file_path_local, &buffer) == 0)
        create = false;

    db->headers_tree_file = fopen(file_path_local, create ? "a+b" : "r+b");
    cstr_free(path_ret, true);
    if (create) {
        // write file-header-magic
        fwrite(file_hdr_magic, 4, 1, db->headers_tree_file);
        uint32_t v = htole32(current_version);
        fwrite(&v, sizeof(v), 1, db->headers_tree_file); /* uint32_t, LE */
    }
    else {
        // check file-header-magic
        uint8_t buf[sizeof(file_hdr_magic)+sizeof(current_version)];
        if ( (uint32_t)buffer.st_size < (uint32_t)(sizeof(file_hdr_magic)+sizeof(current_version)) ||
             fread(buf, sizeof(file_hdr_magic)+sizeof(current_version), 1, db->headers_tree_file) != 1 ||
             memcmp(buf, file_hdr_magic, sizeof(file_hdr_magic))
            )
        {
            fprintf(stderr, "Error reading database file\n");
            return false;
        }
        if (le32toh(*(buf+sizeof(file_hdr_magic))) > current_version) {
            fprintf(stderr, "Unsupported file version\n");
            return false;
        }
    }
    btc_bool firstblock = true;
    size_t connected_headers_count = 0;
    if (db->headers_tree_file && !create)
    {
        while (!feof(db->headers_tree_file))
        {
            uint8_t buf_all[32+4+80];
            if (fread(buf_all, sizeof(buf_all), 1, db->headers_tree_file) == 1) {
                struct const_buffer cbuf_all = {buf_all, sizeof(buf_all)};

                //load all

                /* deserialize the p2p header */
                uint256 hash;
                uint32_t height;
                deser_u256(hash, &cbuf_all);
                deser_u32(&height, &cbuf_all);
                if (height >= 1120874) {
                    //TODO: test hack, remove me
                    continue;
                }
                btc_bool connected;
                if (firstblock)
                {
                    btc_blockindex *chainheader = calloc(1, sizeof(btc_blockindex));
                    chainheader->height = height;
                    if (!btc_block_header_deserialize(&chainheader->header, &cbuf_all)) {
                        btc_free(chainheader);
                        fprintf(stderr, "Error: Invalid data found.\n");
                        return -1;
                    }
                    btc_block_header_hash(&chainheader->header, (uint8_t *)&chainheader->hash);
                    chainheader->prev = NULL;
                    db->chaintip = chainheader;
                    firstblock = false;
                }
                else {
                    btc_headers_db_connect_hdr(db, &cbuf_all, true, &connected);
                    if (!connected)
                    {
                        printf("Connecting header failed (at height: %d)\n", db->chaintip->height);
                    }
                    else {
                        connected_headers_count++;
                    }
                }
            }
        }
    }
    printf("Connected %ld headers, now at height: %d\n",  connected_headers_count, db->chaintip->height);
    return (db->headers_tree_file != NULL);
}

btc_bool btc_headers_db_write(btc_headers_db* db, btc_blockindex *blockindex) {
    cstring *rec = cstr_new_sz(100);
    ser_u256(rec, blockindex->hash);
    ser_u32(rec, blockindex->height);
    btc_block_header_serialize(rec, &blockindex->header);
    size_t res = fwrite(rec->str, rec->len, 1, db->headers_tree_file);
    btc_file_commit(db->headers_tree_file);
    cstr_free(rec, true);
    return (res == 1);
}

btc_blockindex * btc_headers_db_connect_hdr(btc_headers_db* db, struct const_buffer *buf, btc_bool load_process, btc_bool *connected) {
    *connected = false;

    btc_blockindex *blockindex = btc_calloc(1, sizeof(btc_blockindex));
    if (!btc_block_header_deserialize(&blockindex->header, buf)) return NULL;

    /* calculate block hash */
    btc_block_header_hash(&blockindex->header, (uint8_t *)&blockindex->hash);

    btc_blockindex *connect_at = NULL;
    btc_blockindex *fork_from_block = NULL;
    /* try to connect it to the chain tip */
    if (memcmp(blockindex->header.prev_block, db->chaintip->hash, BTC_HASH_LENGTH) == 0)
    {
        connect_at = db->chaintip;
    }
    else {
        // check if we know the prevblock
        fork_from_block = btc_headersdb_find(db, blockindex->header.prev_block);
        if (fork_from_block) {
            /* block found */
            printf("Block found on a fork...\n");
            connect_at = fork_from_block;
        }
    }

    if (connect_at != NULL) {
        /* TODO: check claimed PoW */
        blockindex->prev = connect_at;
        blockindex->height = connect_at->height+1;

        /* TODO: check if we should switch to the fork with most work (instead of height) */
        if (blockindex->height > db->chaintip->height) {
            if (fork_from_block) {
                /* TODO: walk back to the fork point and call reorg callback */
                printf("Switch to the fork!\n");
            }
            db->chaintip = blockindex;
        }
        /* store in db */
        if (!load_process && db->read_write_file)
        {
            if (!btc_headers_db_write(db, blockindex)) {
                fprintf(stderr, "Error writing blockheader to database\n");
            }
        }
        if (db->use_binary_tree) {
            btc_blockindex *retval = tsearch(blockindex, &db->tree_root, btc_header_compare);
        }

        if (db->max_hdr_in_mem > 0) {
            // de-allocate no longer required headers
            // keep them only on-disk
            btc_blockindex *scan_tip = db->chaintip;
            for(unsigned int i = 0; i<db->max_hdr_in_mem+1;i++)
            {
                if (scan_tip->prev)
                    scan_tip = scan_tip->prev;
                else {
                    break;
                }

                if (scan_tip && i == db->max_hdr_in_mem && scan_tip != &db->genesis) {
                    if (scan_tip->prev && scan_tip->prev != &db->genesis) {
                        tdelete(scan_tip->prev, &db->tree_root, btc_header_compare);
                        btc_free(scan_tip->prev);

                        scan_tip->prev = NULL;
                        db->chainbottom = scan_tip;
                    }
                }
            }
        }
        *connected = true;
    }
    else {
        //TODO, add to orphans
        char hex[65] = {0};
        utils_bin_to_hex(blockindex->hash, BTC_HASH_LENGTH, hex);
        printf("Failed connecting header at height %d (%s)\n", db->chaintip->height, hex);
    }

    return blockindex;
}

void btc_headers_db_fill_block_locator(btc_headers_db* db, vector *blocklocators)
{
    btc_blockindex *scan_tip = db->chaintip;
    if (scan_tip->height > 0)
    {
        for(int i = 0; i<10;i++)
        {
            //TODO: try to share memory and avoid heap allocation
            uint256 *hash = btc_calloc(1, sizeof(uint256));
            memcpy(hash, scan_tip->hash, sizeof(uint256));

            vector_add(blocklocators, (void *)hash);
            if (scan_tip->prev)
                scan_tip = scan_tip->prev;
            else
                break;
        }
    }
}

btc_blockindex * btc_headersdb_find(btc_headers_db* db, uint256 hash) {
    if (db->use_binary_tree)
    {
        btc_blockindex *blockindex = btc_calloc(1, sizeof(btc_blockindex));
        memcpy(blockindex->hash, hash, sizeof(uint256));
        btc_blockindex *blockindex_f = tfind(blockindex, &db->tree_root, btc_header_compare); /* read */
        if (blockindex_f) {
            blockindex_f = *(btc_blockindex **)blockindex_f;
        }
        btc_free(blockindex);
        return blockindex_f;
    }
    return NULL;
}

btc_blockindex * btc_headersdb_getchaintip(btc_headers_db* db) {
    return db->chaintip;
}

btc_bool btc_headersdb_disconnect_tip(btc_headers_db* db) {
    if (db->chaintip->prev)
    {
        btc_blockindex *oldtip = db->chaintip;
        db->chaintip = db->chaintip->prev;
        /* disconnect/remove the chaintip */
        tdelete(oldtip, &db->tree_root, btc_header_compare);
        btc_free(oldtip);
        return true;
    }
    return false;
}

btc_bool btc_headersdb_has_checkpoint_start(btc_headers_db* db) {
    return (db->chainbottom->height != 0);
}

void btc_headersdb_set_checkpoint_start(btc_headers_db* db, uint256 hash, uint32_t height) {
    db->chainbottom = btc_calloc(1, sizeof(btc_blockindex));
    db->chainbottom->height = height;
    memcpy(db->chainbottom->hash, hash, sizeof(uint256));
    db->chaintip = db->chainbottom;
}
