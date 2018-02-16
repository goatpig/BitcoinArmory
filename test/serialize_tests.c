/**********************************************************************
 * Copyright (c) 2015 Jonas Schnelli                                  *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btc/cstr.h>
#include <btc/serialize.h>
#include <btc/utils.h>

void test_serialize()
{
    cstring* s3 = cstr_new("foo");
    cstring* s2 = cstr_new_sz(200);
    struct const_buffer buf2;
    uint16_t num0;
    uint32_t num1;
    int32_t  num1a;
    uint64_t num2;
    int64_t  num2a;
    uint32_t num3;
    char strbuf[255];
    cstring* deser_test;
    struct const_buffer buf3;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int32_t i32;
    uint256 hash = {0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03,0x00,0x01,0x02,0x03};
    uint256 hashc;

    ser_u16(s2, 0xAAFF);
    ser_u32(s2, 0xFFFFFFFF);
    ser_s32(s2, 0xFFFFFFFF);
    ser_u64(s2, 0x99FF99FFDDBBAAFF);
    ser_s64(s2, 0x99FF99FFDDBBAAFF);
    ser_varlen(s2, 10);
    ser_varlen(s2, 1000);
    ser_varlen(s2, 100000000);
    ser_str(s2, "test", 4);
    ser_varstr(s2, s3);
    ser_u256(s2, hash);
    cstr_free(s3, true);

    buf2.p = s2->str; buf2.len = s2->len;
    deser_u16(&num0, &buf2);
    assert(num0 == 43775); /* 0xAAFF */
    deser_u32(&num1, &buf2);
    assert(num1 == 4294967295); /* 0xFFFFFFFF */
    deser_s32(&num1a, &buf2);
    assert(num1a == -1); /* 0xFFFFFFFF signed */

    deser_u64(&num2, &buf2);
    assert(num2 == 0x99FF99FFDDBBAAFF); /* 0x99FF99FFDDBBAAFF */
    deser_s64(&num2a, &buf2);
    assert(num2a == (int64_t)0x99FF99FFDDBBAAFF); /* 0x99FF99FFDDBBAAFF */

    deser_varlen(&num3, &buf2);
    assert(num3 == 10);
    deser_varlen(&num3, &buf2);
    assert(num3 == 1000);
    deser_varlen(&num3, &buf2);
    assert(num3 == 100000000);

    deser_str(strbuf, &buf2, 255);
    assert(strncmp(strbuf, "test", 4) == 0);
    deser_test = cstr_new_sz(0);
    deser_varstr(&deser_test, &buf2);
    assert(strncmp(deser_test->str, "foo", 3) == 0);

    deser_u256(hashc, &buf2);
    assert(memcmp(hash, hashc, 32) == 0);
    cstr_free(deser_test, true);

    cstr_free(s2, true);

    buf3.p = NULL, buf3.len = 0;

    assert(deser_u16(&u16, &buf3) == false);
    assert(deser_u32(&u32, &buf3) == false);
    assert(deser_u64(&u64, &buf3) == false);
    assert(deser_s32(&i32, &buf3) == false);
}

