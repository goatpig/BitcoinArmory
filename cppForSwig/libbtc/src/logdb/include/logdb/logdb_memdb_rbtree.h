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

/*
 Memory mapping with a red black tree
*/

#ifndef __LIBLOGDB_RBTREE_H__
#define __LIBLOGDB_RBTREE_H__

#include <btc/cstr.h>
#include <logdb/logdb_base.h>
#include <logdb/logdb_rec.h>
#include <logdb/logdb_core.h>
#include <logdb/red_black_tree.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct logdb_rbtree_db_ {
    struct rb_red_blk_tree *tree;
} logdb_rbtree_db;

logdb_rbtree_db* logdb_rbtree_db_new();
void logdb_rbtree_free(void *ctx);

/** appends record to the rbtree */
LIBLOGDB_API void logdb_rbtree_init(logdb_log_db* db);

/** appends record to the rbtree */
LIBLOGDB_API void logdb_rbtree_append(void* ctx, logdb_bool load_phase, logdb_record *rec);

/** find a record by key */
LIBLOGDB_API cstring * logdb_rbtree_find(logdb_log_db* db, cstring *key);

/** count all red black tree nodes */
LIBLOGDB_API size_t logdb_rbtree_size(logdb_log_db* db);

/* static interface */
static logdb_memmapper logdb_rbtree_mapper = {
    logdb_rbtree_append,
    logdb_rbtree_init,
    logdb_rbtree_free,
    logdb_rbtree_find,
    logdb_rbtree_size
};

    
#ifdef __cplusplus
}
#endif

#endif /* __LIBLOGDB_RBTREE_H__ */
