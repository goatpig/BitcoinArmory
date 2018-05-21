/**********************************************************************
 * Copyright (c) Copyright 2015 BitPay, Inc.                          *
 * Copyright (c) Copyright 2015 Jonas Schnelli                        *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btc/cstr.h>

void test_cstr()
{
    cstring* s1 = cstr_new("foo");
    cstring* s2 = cstr_new("foo");
    cstring* s3 = cstr_new("bar");
    cstring* s4 = cstr_new("bar1");
    
    cstring* s = cstr_new("foo");
    assert(s != NULL);
    assert(s->len == 3);
    assert(strcmp(s->str, "foo") == 0);

    cstr_free(s, true);

    s = cstr_new_sz(200);
    assert(s != NULL);
    assert(s->alloc > 200);
    assert(s->len == 0);

    cstr_free(s, true);

    s = cstr_new_buf("foo", 2);
    assert(s != NULL);
    assert(s->len == 2);
    assert(strcmp(s->str, "fo") == 0);

    cstr_free(s, true);

    s = cstr_new(NULL);
    assert(s != NULL);
    cstr_append_buf(s, "f", 1);
    cstr_append_buf(s, "o", 1);
    cstr_append_buf(s, "o", 1);
    assert(s->len == 3);
    assert(strcmp(s->str, "foo") == 0);

    cstr_free(s, true);

    s = cstr_new("foo");
    assert(s != NULL);

    cstr_resize(s, 2);
    cstr_resize(s, 2);
    cstr_alloc_minsize(s, 2);
    cstr_alloc_minsize(s, 1);

    assert(s->len == 2);
    assert(strcmp(s->str, "fo") == 0);

    cstr_resize(s, 4);
    assert(s->len == 4);
    assert(s->alloc > 4);
    memcpy(s->str, "food", 4);
    assert(strcmp(s->str, "food") == 0);

    cstr_free(s, true);

    assert(cstr_compare(s1, s2) ==  0);
    assert(cstr_compare(s1, s3) ==  1);
    assert(cstr_compare(s3, s1) ==  -1);
    assert(cstr_compare(s3, s4) ==  -1);
    assert(cstr_compare(s4, s3) ==  1);

    assert(cstr_equal(s1, s2) == true);
    assert(cstr_equal(s1, s3) == false);
    assert(cstr_equal(s1, NULL) == false);
    assert(cstr_equal(s2, s3) == false);
    assert(cstr_equal(s3, s3) == true);
    assert(cstr_equal(s3, s4) == false);
    cstr_erase(s4, 0, 3);
    cstr_erase(s4, 110, 3);
    cstr_erase(s4, s4->len, 0);
    cstr_erase(s4, 0, 100);
    assert(strcmp(s4->str, "1") == 0);

    cstr_free(s1, true);
    cstr_free(s2, true);
    cstr_free(s3, true);
    cstr_free(s4, true);
}
