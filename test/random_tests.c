/**********************************************************************
* Copyright (c) 2015 Jonas Schnelli                                  *
* Distributed under the MIT software license, see the accompanying   *
* file COPYING or http://www.opensource.org/licenses/mit-license.php.*
**********************************************************************/

#include <btc/random.h>

#include "utest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void test_random_init_cb(void)
{

}


btc_bool test_random_bytes_cb(uint8_t* buf, uint32_t len, const uint8_t update_seed) {

    (void)(update_seed);
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = 0;
    }
    return false;
}

void test_random()
{
    unsigned char r_buf[32];
    memset(r_buf, 0, 32);
    btc_random_init();
    u_assert_int_eq(btc_random_bytes(r_buf, 32, 0), true);

    btc_rnd_mapper mymapper = {test_random_init_cb, test_random_bytes_cb};
    btc_rnd_set_mapper(mymapper);

    u_assert_int_eq(btc_random_bytes(r_buf, 32, 0), false);

    for (uint8_t i = 0; i < 32; i++) {
        u_assert_int_eq(r_buf[i], 0);
    }

    // switch back to the default random callback mapper
    btc_rnd_set_mapper_default();
}
