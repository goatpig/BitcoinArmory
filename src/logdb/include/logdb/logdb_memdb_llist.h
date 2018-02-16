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
 A simple linkes list memory DB,
 extremly slow and only for callback demo purposes
 
 If you are going to write a memory mapper for logdb, use a red black tree
 http://web.mit.edu/~emin/Desktop/ref_to_emin/www.old/source_code/red_black_tree/index.html
 
 Logdb does currently not provide an efficient memory map
*/

#ifndef __LIBLOGDB_MEMDB_H__
#define __LIBLOGDB_MEMDB_H__

#include <btc/cstr.h>
#include <logdb/logdb_base.h>
#include <logdb/logdb_rec.h>
#include <logdb/logdb_core.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct logdb_llist_db_ {
    logdb_record *head;
} logdb_llist_db;

logdb_llist_db* logdb_llist_db_new();
void logdb_llist_db_free(void *ctx);

LIBLOGDB_API void logdb_llistdb_init(logdb_log_db* db);

/** appends record to the mem db */
LIBLOGDB_API void logdb_llistdb_append(void* ctx, logdb_bool load_phase, logdb_record *rec);

LIBLOGDB_API cstring * logdb_llistdb_find(logdb_log_db* db, cstring *key);

LIBLOGDB_API size_t logdb_llistdb_size(logdb_log_db* db);

LIBLOGDB_API void logdb_llistdb_cleanup(void* ctx);

/* static interface */
static logdb_memmapper logdb_llistdb_mapper = {
    logdb_llistdb_append,
    logdb_llistdb_init,
    logdb_llist_db_free,
    logdb_llistdb_find,
    logdb_llistdb_size
};

#ifdef __cplusplus
}
#endif

#endif /* __LIBLOGDB_MEMDB_H__ */
