/**********************************************************************
 * Copyright (c) 2015 Jonas Schnelli                                  *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <logdb/logdb.h>
#include <btc/utils.h>

#include "../../../test/utest.h"

#include <unistd.h>
#include <errno.h>


#include "logdb_tests_sample.h"

static const char *dbtmpfile = "/tmp/dummy";

static const char *key1str = "ALorem ipsum dolor sit amet, consectetur adipisicing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

static const char *value1str = "BLorem ipsum dolor sit amet, consectetur adipisicing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

void test_logdb(logdb_log_db* (*new_func)())
{
    logdb_log_db *db;
    enum logdb_error error = 0;
    cstring *key;// key= {"key0", 4};
    cstring *value;// = {"val0", 4};
    cstring *key1;
    cstring *value1;
    cstring *outtest;
    cstring *value_test;
    unsigned char testbin[4] = {0x00, 0x10, 0x20, 0x30};
    cstring *value0;// = {"dumb", 4};
    cstring *key2;// = {"pkey", 4};
    cstring *value2;
    cstring *smp_value;
    cstring *smp_key;
    uint8_t txbin[10240];
    uint8_t txbin_rev[10240];
    char hexrev[98];
    int outlenrev;
    long fsize;
    uint8_t *buf;
    uint8_t *wrk_buf;
    FILE *f;
    unsigned int i;
    char bufs[300][65];
    rb_red_blk_node *nodetest;
    unsigned int cnt = 0;
    logdb_record* rec;

    key = cstr_new("key0");
    value = cstr_new("val0");

    value0 = cstr_new("dumb");
    value1 = cstr_new_sz(10);
    value2 = cstr_new_sz(10);
    key1 = cstr_new_sz(10);
    key2 = cstr_new("key2");

    cstr_append_buf(value2, testbin, sizeof(testbin));
    cstr_append_buf(value2, testbin, sizeof(testbin));
    cstr_append_buf(key1, key1str, strlen(key1str));
    cstr_append_buf(value1, value1str, strlen(value1str));

    unlink(dbtmpfile);
    db = new_func();
    u_assert_int_eq(logdb_load(db, "file_that_should_not_exists.dat", false, NULL), false);
    u_assert_int_eq(logdb_load(db, dbtmpfile, true, NULL), true);

    logdb_append(db, NULL, key, value);
    logdb_append(db, NULL, key1, value1);

    u_assert_int_eq(logdb_cache_size(db), 2);
    outtest = logdb_find_cache(db, key1);
    u_assert_int_eq(strcmp(outtest->str, value1str),0);
    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);
    u_assert_int_eq(logdb_count_keys(db), 2);
    
    value_test = logdb_find(db, key1);
    u_assert_int_eq(strcmp(value_test->str, value1str), 0);
    value_test = logdb_find(db, key);
    u_assert_int_eq(memcmp(value_test->str, value->str, value->len), 0);
    logdb_free(db);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);



    logdb_append(db, NULL, key2, value2);
    logdb_flush(db);
    logdb_free(db);

    /* check if private key is available */
    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);

    value_test = logdb_find(db, key2);
    u_assert_int_eq(memcmp(value_test->str, value2->str, value2->len), 0);
    value_test = logdb_find(db, key);
    u_assert_int_eq(memcmp(value_test->str, value->str, value->len), 0);

    /* delete a record */
    logdb_delete(db, NULL, key2);
    logdb_flush(db);
    logdb_free(db);

    /* find and check the deleted record */
    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);

    value_test = logdb_find(db, key);
    u_assert_int_eq(memcmp(value_test->str, value->str, value->len), 0);

    value_test = logdb_find(db, key2);
    u_assert_is_null(value_test);

    /* overwrite a key */
    logdb_append(db, NULL, key, value0);

    value_test = logdb_find(db, key);
    u_assert_int_eq(memcmp(value_test->str, value0->str, value0->len), 0);

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);
    value_test = logdb_find(db, key);
    u_assert_int_eq(memcmp(value_test->str, value0->str, value0->len), 0);

    logdb_flush(db);
    logdb_free(db);




    /* simulate corruption */
    f = fopen(dbtmpfile, "rb");
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = malloc(fsize + 1);
    fread(buf, fsize, 1, f);
    fclose(f);

    /* ---------------------------------------------------- */
    wrk_buf = safe_malloc(fsize + 1);
    memcpy(wrk_buf, buf, fsize);
    wrk_buf[0] = 0x88; /* wrong header */

    unlink(dbtmpfile);
    f = fopen(dbtmpfile, "wb");
    fwrite(wrk_buf, 1, fsize, f);
    fclose(f);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), false);
    u_assert_int_eq(error, LOGDB_ERROR_WRONG_FILE_FORMAT);
    logdb_free(db);

    /* ---------------------------------------------------- */
    memcpy(wrk_buf, buf, fsize);
    wrk_buf[66] = 0x00; /* wrong checksum hash */

    unlink(dbtmpfile);
    f = fopen(dbtmpfile, "wb");
    fwrite(wrk_buf, 1, fsize, f);
    fclose(f);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), false);
    u_assert_int_eq(error, LOGDB_ERROR_CHECKSUM);
    logdb_free(db);

    /* ---------------------------------------------------- */
    memcpy(wrk_buf, buf, fsize);
    wrk_buf[42] = 0xFF; /* wrong value length */

    unlink(dbtmpfile);
    f = fopen(dbtmpfile, "wb");
    fwrite(wrk_buf, 1, fsize, f);
    fclose(f);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), false);
    u_assert_int_eq(error, LOGDB_ERROR_DATASTREAM_ERROR);
    logdb_free(db);

    free(buf);
    free(wrk_buf);


    /* --- large db test */
    unlink(dbtmpfile);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, true, NULL), true);

    smp_key = cstr_new_sz(100);
    smp_value = cstr_new_sz(100);
    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);

        outlen = sizeof(tx->hextx) / 2;
        utils_hex_to_bin(tx->hextx, txbin, strlen(tx->hextx), &outlen);

        cstr_erase(smp_value, 0, smp_value->len);
        cstr_append_buf(smp_value, txbin, outlen);

        logdb_append(db, NULL, smp_key, smp_value);
    }

    u_assert_int_eq(logdb_count_keys(db), (sizeof(sampledata) / sizeof(sampledata[0])));

    /* check all records */
    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);
        outtest = logdb_find(db, smp_key);

        outlen = sizeof(tx->hextx) / 2;
        utils_hex_to_bin(tx->hextx, txbin, strlen(tx->hextx), &outlen);

        u_assert_int_eq(outlen, outtest->len);
    }

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    error = 0;
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), true);
    u_assert_int_eq(logdb_count_keys(db), (sizeof(sampledata) / sizeof(sampledata[0])));

    /* check all records */
    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        memcpy(hexrev, tx->txhash, sizeof(tx->txhash));
        utils_reverse_hex(hexrev, strlen(tx->txhash));
        outlenrev = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(hexrev, txbin_rev, strlen(hexrev), &outlenrev);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);
        outtest = logdb_find(db, smp_key);

        outlen = strlen(tx->hextx) / 2;
        utils_hex_to_bin(tx->hextx, txbin, strlen(tx->hextx), &outlen);
        u_assert_int_eq(outlen, outtest->len);

        /*  hash transaction data and check hashes */
        if (strlen(tx->hextx) > 2)
        {
            uint8_t tx_hash_check[SHA256_DIGEST_LENGTH];
            sha256_Raw(txbin, outlen, tx_hash_check);
            sha256_Raw(tx_hash_check, 32, tx_hash_check);
            u_assert_int_eq(memcmp(tx_hash_check, txbin_rev, SHA256_DIGEST_LENGTH), 0);
        }

    }

    /* check all records */
    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);
        logdb_delete(db, NULL, smp_key);
    }
    u_assert_int_eq(logdb_count_keys(db), 0);

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    error = 0;
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), true);
    u_assert_int_eq(error, LOGDB_SUCCESS);
    u_assert_int_eq(logdb_count_keys(db), 0);

    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);

        outlen = sizeof(tx->hextx) / 2;
        utils_hex_to_bin(tx->hextx, txbin, strlen(tx->hextx), &outlen);

        cstr_erase(smp_value, 0, smp_value->len);
        cstr_append_buf(smp_value, txbin, outlen);

        logdb_append(db, NULL, smp_key, smp_value);
    }

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    error = 0;
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), true);
    u_assert_int_eq(error, LOGDB_SUCCESS);
    u_assert_int_eq(logdb_count_keys(db), (sizeof(sampledata) / sizeof(sampledata[0])));

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    error = 0;
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, &error), true);
    u_assert_int_eq(error, LOGDB_SUCCESS);
    u_assert_int_eq(logdb_count_keys(db), (sizeof(sampledata) / sizeof(sampledata[0])));

    if(new_func == logdb_rbtree_new)
    {
        logdb_rbtree_db* handle = (logdb_rbtree_db *)db->cb_ctx;
        size_t size = rbtree_count(handle->tree);

        nodetest = NULL;
        while ((nodetest = rbtree_enumerate_next(handle->tree)))
        {
            rec = (logdb_record *)nodetest->info;
            utils_bin_to_hex((unsigned char *)rec->key->str, rec->key->len, bufs[cnt]);

            for(i = 0; i < cnt; i++)
            {
                u_assert_int_eq(strcmp(bufs[i], bufs[cnt]) != 0, 1);
            }
            cnt++;
        }
        u_assert_int_eq(size, cnt);
    }

    for (i = 0; i < (sizeof(sampledata) / sizeof(sampledata[0])); i++) {
        const struct txtest *tx = &sampledata[i];

        uint8_t hashbin[sizeof(tx->txhash) / 2];
        int outlen = sizeof(tx->txhash) / 2;
        utils_hex_to_bin(tx->txhash, hashbin, strlen(tx->txhash), &outlen);

        cstr_erase(smp_key, 0, smp_key->len);
        cstr_append_buf(smp_key, hashbin, outlen);

        outlen = sizeof(tx->hextx) / 2;
        utils_hex_to_bin(tx->hextx, txbin, strlen(tx->hextx), &outlen);

        cstr_erase(smp_value, 0, smp_value->len);
        cstr_append_buf(smp_value, txbin, outlen);

        logdb_append(db, NULL, smp_key, smp_value);
    }

    logdb_flush(db);
    logdb_free(db);

    /* test switch mem mapper after initialitaion. */
    db = logdb_new();
    logdb_set_memmapper(db, &logdb_rbtree_mapper, NULL);
    logdb_flush(db);
    logdb_free(db);


    unlink(dbtmpfile);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, true, NULL), true);

    // create transaction, don't store
    logdb_txn* txn = logdb_txn_new();
    logdb_append(db, txn, key, value);
    logdb_append(db, txn, key1, value1);
    u_assert_int_eq(logdb_cache_size(db), 0);
    logdb_txn_free(txn);

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);
    // db should still be empty
    u_assert_int_eq(logdb_count_keys(db), 0);

    // create transaction, store it this time
    txn = logdb_txn_new();
    logdb_append(db, txn, key, value);
    logdb_append(db, txn, key1, value1);
    logdb_txn_commit(db, txn);
    u_assert_int_eq(logdb_cache_size(db), 2);
    logdb_txn_free(txn);

    logdb_flush(db);
    logdb_free(db);

    db = new_func();
    u_assert_int_eq(logdb_load(db, dbtmpfile, false, NULL), true);
    // now we should have the two persisted items from the txn
    u_assert_int_eq(logdb_count_keys(db), 2);
    logdb_flush(db);
    logdb_free(db);

    cstr_free(key, true);
    cstr_free(value, true);
    cstr_free(value0, true);
    cstr_free(value1, true);
    cstr_free(value2, true);
    cstr_free(key1, true);
    cstr_free(key2, true);
    cstr_free(smp_key, true);
    cstr_free(smp_value, true);
}

void test_logdb_rbtree()
{
    test_logdb(logdb_rbtree_new);
}

void test_logdb_memdb()
{
    test_logdb(logdb_new);
}

void test_examples()
{
    cstring *value;
    cstring *key;
    logdb_log_db *db;
    enum logdb_error error = 0;
    int create_database = 1;

    /* create a new database object */
    db = logdb_new();

    /* create a database file (will overwrite existing) */
    logdb_load(db, "/tmp/test.logdb", create_database, &error);

    /* append a record */
    value = cstr_new("testkey");
    key = cstr_new("somevalue");
    /* second parameter NULL means we don't use a transaction */
    logdb_append(db, NULL, key, value);

    /* flush database (write its state to disk) */
    logdb_flush(db);

    /* cleanup */
    logdb_free(db);
    cstr_free(key, true);
    cstr_free(value, true);
}
