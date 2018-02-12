/**********************************************************************
 * Copyright (c) 2016 Jonas Schnelli                                  *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btc/chainparams.h>
#include <btc/utils.h>
#include <btc/protocol.h>
#include "utest.h"

#include <event2/util.h>

void test_protocol()
{
    /* get new string buffer */
    cstring *version_msg_cstr = cstr_new_sz(256);
    cstring *inv_msg_cstr = cstr_new_sz(256);

    struct sockaddr_in test_sa, test_sa_check;
    memset(&test_sa, 0, sizeof(test_sa));
    memset(&test_sa_check, 0, sizeof(test_sa_check));
    test_sa.sin_family = AF_INET;
    struct sockaddr_in6 test_sa6, test_sa6_check;
    test_sa6.sin6_family = AF_INET6;
    test_sa6.sin6_port = htons(1024);
    evutil_inet_pton(AF_INET, "10.0.0.1", &test_sa.sin_addr); // store IP in antelope

    char i6buf[1024];
    memset(&i6buf, 0, 1024);

    evutil_inet_pton(AF_INET6, "::1", &test_sa6.sin6_addr);
    btc_p2p_address ipv6Test;
    btc_p2p_address_init(&ipv6Test);
    btc_addr_to_p2paddr((struct sockaddr *)&test_sa6, &ipv6Test);
    btc_p2paddr_to_addr(&ipv6Test, (struct sockaddr *)&test_sa6_check);
    memset(&i6buf, 0, 1024);
    u_assert_int_eq(test_sa6.sin6_port, test_sa6_check.sin6_port);

    /* copy socket_addr to p2p addr */
    btc_p2p_address fromAddr;
    btc_p2p_address_init(&fromAddr);
    btc_p2p_address toAddr;
    btc_p2p_address_init(&toAddr);
    btc_addr_to_p2paddr((struct sockaddr *)&test_sa, &toAddr);
    btc_p2paddr_to_addr(&toAddr, (struct sockaddr *)&test_sa_check);
    u_assert_int_eq(test_sa.sin_port, test_sa_check.sin_port);
    evutil_inet_ntop(AF_INET, &test_sa_check.sin_addr, i6buf, 1024);
    u_assert_str_eq(i6buf, "10.0.0.1");

    /* create a inv message struct */
    btc_p2p_inv_msg inv_msg, inv_msg_check;
    memset(&inv_msg, 0, sizeof(inv_msg));

    uint256 hash = {0};

    btc_p2p_msg_inv_init(&inv_msg, 1, hash);
    btc_p2p_msg_inv_ser(&inv_msg, inv_msg_cstr);

    struct const_buffer buf_inv = {inv_msg_cstr->str, inv_msg_cstr->len};
    u_assert_int_eq(btc_p2p_msg_inv_deser(&inv_msg_check, &buf_inv), true);
    u_assert_int_eq(inv_msg_check.type, 1);
    u_assert_mem_eq(inv_msg_check.hash, inv_msg.hash, sizeof(inv_msg.hash));
    cstr_free(inv_msg_cstr, true);

    /* create a version message struct */
    btc_p2p_version_msg version_msg;
    memset(&version_msg, 0, sizeof(version_msg));

    /* create a serialized version message */
    btc_p2p_msg_version_init(&version_msg, &fromAddr, &toAddr, "client", false);
    btc_p2p_msg_version_ser(&version_msg, version_msg_cstr);

    /* create p2p message */
    cstring *p2p_msg = btc_p2p_message_new((unsigned const char *)&btc_chainparams_main.netmagic, BTC_MSG_VERSION, version_msg_cstr->str, version_msg_cstr->len);

    struct const_buffer buf = {p2p_msg->str, p2p_msg->len};
    btc_p2p_msg_hdr hdr;
    btc_p2p_deser_msghdr(&hdr, &buf);

    u_assert_mem_eq(hdr.netmagic, &btc_chainparams_main.netmagic, 4);
    u_assert_str_eq(hdr.command, BTC_MSG_VERSION);
    u_assert_int_eq(hdr.data_len, version_msg_cstr->len);
    u_assert_int_eq(buf.len, hdr.data_len);
    u_assert_int_eq(buf.len, hdr.data_len);
    u_assert_mem_eq(buf.p, version_msg_cstr->str, hdr.data_len);

    btc_p2p_version_msg v_msg_check;
    u_assert_int_eq(btc_p2p_msg_version_deser(&v_msg_check, &buf), true);

    u_assert_int_eq(v_msg_check.version, BTC_PROTOCOL_VERSION);
    u_assert_str_eq(v_msg_check.useragent, "client");
    u_assert_int_eq(v_msg_check.start_height, 0);

    cstr_free(p2p_msg, true);
    cstr_free(version_msg_cstr, true);

    /* getheaders */
    uint256 genesis_hash = {0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0xd6, 0x68, 0x9c, 0x08, 0x5a, 0xe1, 0x65, 0x83, 0x1e, 0x93, 0x4f, 0xf7, 0x63, 0xae, 0x46, 0xa2, 0xa6, 0xc1, 0x72, 0xb3, 0xf1, 0xb6, 0x0a, 0x8c, 0xe2, 0x6f};
    vector *blocklocators = vector_new(1, NULL);
    vector_add(blocklocators, genesis_hash);
    cstring *getheader_msg = cstr_new_sz(256);
    btc_p2p_msg_getheaders(blocklocators, NULL, getheader_msg);
    p2p_msg = btc_p2p_message_new((unsigned const char *)&btc_chainparams_main.netmagic, BTC_MSG_GETHEADERS, getheader_msg->str, getheader_msg->len);


    buf.p = p2p_msg->str;
    buf.len = p2p_msg->len;
    btc_p2p_deser_msghdr(&hdr, &buf);
    u_assert_str_eq(hdr.command, BTC_MSG_GETHEADERS);
    u_assert_int_eq(hdr.data_len, getheader_msg->len);


    uint256 hashstop_check;
    vector *blocklocators_check = vector_new(1, free);
    btc_p2p_deser_msg_getheaders(blocklocators_check, hashstop_check, &buf);
    u_assert_mem_eq(NULLHASH, hashstop_check, sizeof(hashstop_check));
    uint8_t *hash_loc_0 = vector_idx(blocklocators_check, 0);
    u_assert_mem_eq(genesis_hash, hash_loc_0, sizeof(genesis_hash));


    /* cleanup */
    cstr_free(getheader_msg, true);
    vector_free(blocklocators, true);
    vector_free(blocklocators_check, true);
    cstr_free(p2p_msg, true);
}

