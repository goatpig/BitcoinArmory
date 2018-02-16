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

#ifndef __LIBLOGDB_LOGDB_H__
#define __LIBLOGDB_LOGDB_H__

#include <logdb/logdb_base.h>
#include <logdb/logdb_rec.h>
#include <btc/sha2.h>

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** error types */
enum logdb_error {
    LOGDB_SUCCESS = 0,
    LOGDB_ERROR_UNKNOWN = 100,
    LOGDB_ERROR_FOPEN_FAILED = 200,
    LOGDB_ERROR_WRONG_FILE_FORMAT = 300,
    LOGDB_ERROR_DATASTREAM_ERROR = 400,
    LOGDB_ERROR_CHECKSUM = 500,
    LOGDB_ERROR_FILE_ALREADY_OPEN = 600
};

typedef struct logdb_memmapper_ logdb_memmapper; /* forware declaration */

/** logdb handle */
typedef struct logdb_log_db {
    FILE *file;
    logdb_memmapper *mem_mapper;
    void *cb_ctx;
    logdb_record *cache_head;
    SHA256_CTX hashctx;
    uint8_t hashlen;
    uint32_t version;
    uint32_t support_flags;
} logdb_log_db;

typedef struct logdb_txn {
    logdb_record *txn_head;
} logdb_txn;

/* function pointer interface for flexible memory mapping functions*/
struct logdb_memmapper_
{
    /* callback called when appending (incl. deletes) a record 
       the 2nd parameter (bool) tells the cb if the record was added
       during the load wallet phase
     */
    void (*append_cb)(void*, logdb_bool, logdb_record *);

    /* callback called when initializing the database */
    void (*init_cb)(logdb_log_db*);

    /* callback called when database gets destroyed */
    void (*cleanup_cb)(void*);

    /* callback for finding a record with given key */
    cstring* (*find_cb)(logdb_log_db*, cstring *);

    /* callback which expect the get back the total amount of keys in the database */
    size_t (*size_cb)(logdb_log_db*);
};

/* DB HANDLING
////////////////////////////////// */
/** creates new logdb handle, sets default values */
LIBLOGDB_API logdb_log_db* logdb_new();

/** creates new logdb handle, sets default values 
    used red black tree for memory mapping */
LIBLOGDB_API logdb_log_db* logdb_rbtree_new();

/** frees database and all in-memory records, closes file if open */
LIBLOGDB_API void logdb_free(logdb_log_db* db);

/** set the callback for all memory mapping operations
    the callback will be called when a record will be loaded from disk, appended, deleted 
    this will allow to do a application specific memory mapping
 */
LIBLOGDB_API void logdb_set_memmapper(logdb_log_db* db, logdb_memmapper *mapper, void *ctx);

/** loads given file as database (memory mapping) */
LIBLOGDB_API logdb_bool logdb_load(logdb_log_db* handle, const char *file_path, logdb_bool create, enum logdb_error *error);

/** flushes database: writes down new records */
LIBLOGDB_API logdb_bool logdb_flush(logdb_log_db* db);

/** deletes record with key */
LIBLOGDB_API void logdb_delete(logdb_log_db* db, logdb_txn *txn, cstring *key);

/** appends record to the logdb */
LIBLOGDB_API void logdb_append(logdb_log_db* db, logdb_txn *txn, cstring *key, cstring *value);

/** find and get value from key */
LIBLOGDB_API cstring * logdb_find_cache(logdb_log_db* db, cstring *key);
LIBLOGDB_API cstring * logdb_find(logdb_log_db* db, cstring *key);

/** get the amount of in-memory-records */
LIBLOGDB_API size_t logdb_cache_size(logdb_log_db* db);
LIBLOGDB_API size_t logdb_count_keys(logdb_log_db* db);

/** writes down single record, internal */
void logdb_write_record(logdb_log_db* db, logdb_record *rec);

/** deserializes next logdb record from file */
logdb_bool logdb_record_deser_from_file(logdb_record* rec, logdb_log_db *db, enum logdb_error *error);

/** remove records with given key (to keep memory clean) */
logdb_bool logdb_remove_existing_records(logdb_record *usehead, cstring *key);


/* TRANSACTION HANDLING
////////////////////////////////// */

/** create a new transaction object **/
LIBLOGDB_API logdb_txn* logdb_txn_new();

/** releases a transaction object **/
LIBLOGDB_API void logdb_txn_free(logdb_txn* db);

/** writes transaction to the internal cache **/
LIBLOGDB_API void logdb_txn_commit(logdb_log_db* db, logdb_txn *txn);
#ifdef __cplusplus
}
#endif

#endif /* __LIBLOGDB_LOGDB_H__ */
