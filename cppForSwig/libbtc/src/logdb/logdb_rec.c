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

#include <logdb/logdb_rec.h>
#include <btc/serialize.h>

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

logdb_record* logdb_record_new()
{
    logdb_record* record;
    record = calloc(1, sizeof(*record));
    record->key = cstr_new_sz(32);
    record->value = cstr_new_sz(128);
    record->written = false;
    record->mode = RECORD_TYPE_WRITE;
    return record;
}

void logdb_record_free(logdb_record* rec)
{
    if (!rec)
        return;

    cstr_free(rec->key, true);
    cstr_free(rec->value, true);
    rec->next = NULL;
    rec->prev = NULL;

    free(rec);
}

void logdb_record_set(logdb_record* rec, cstring *key, cstring *val)
{
    if (key == NULL)
        return;

    if (rec->key)
        cstr_free(rec->key, true);

    rec->key = cstr_new_cstr(key);

    if (rec->value)
    {
        cstr_free(rec->value, true);
        rec->value = 0;
    }
    if (val)
    {
        rec->value = cstr_new_cstr(val);
        rec->mode = RECORD_TYPE_WRITE;
    }
    else
        rec->mode = RECORD_TYPE_ERASE;
}

logdb_record* logdb_record_copy(logdb_record* b_rec)
{
    logdb_record* a_rec = logdb_record_new();
    cstr_append_cstr(a_rec->key, b_rec->key);
    cstr_append_cstr(a_rec->value, b_rec->value);
    a_rec->written = b_rec->written;
    a_rec->mode = b_rec->mode;
    return a_rec;
}

void logdb_record_ser(logdb_record* rec, cstring *buf)
{
    ser_bytes(buf, &rec->mode, 1);
    ser_varlen(buf, rec->key->len);
    ser_bytes(buf, rec->key->str, rec->key->len);

    /* write value for a WRITE operation */
    if (rec->mode == RECORD_TYPE_WRITE)
    {
        ser_varlen(buf, rec->value->len);
        ser_bytes(buf, rec->value->str, rec->value->len);
    }
}

size_t logdb_record_height(logdb_record* head)
{
    size_t cnt = 0;
    logdb_record *rec_loop = head;
    while (rec_loop)
    {
        if (rec_loop->mode == RECORD_TYPE_WRITE)
            cnt++;

        rec_loop = rec_loop->prev;
    }

    return cnt;
}

cstring * logdb_record_find_desc(logdb_record* head, cstring *key)
{
    cstring *found_value = NULL;
    cstring *keycstr;
    logdb_record *rec;

    if (key == NULL)
        return NULL;

    rec = head;
    while (rec)
    {
        if (cstr_equal(rec->key, key))
        {
            /* found */
            found_value = rec->value;

            /* found, but deleted */
            if (rec->mode == RECORD_TYPE_ERASE)
                found_value = NULL;

            break;
        }
        rec = rec->prev;
    }
    return found_value;
}

logdb_record * logdb_record_rm_desc(logdb_record *usehead, cstring *key)
{
    /* remove old records with same key */
    logdb_record *rec_loop = usehead;
    logdb_record *rec_head = usehead;
    while (rec_loop)
    {
        logdb_record *prev_rec = rec_loop->prev;
        if (cstr_equal(rec_loop->key, key))
        {
            /* remove from linked list */
            if (rec_loop->prev)
                rec_loop->prev->next = rec_loop->next;

            if (rec_loop->next && rec_loop->next->prev)
                rec_loop->next->prev = rec_loop->prev;

            /* if we are going to delete the head, report new head */
            if (rec_loop == usehead)
                rec_head = rec_loop->prev;

            logdb_record_free(rec_loop);
        }
        
        rec_loop = prev_rec;
    }
    return rec_head;
}
