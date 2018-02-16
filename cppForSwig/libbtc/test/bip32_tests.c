/**********************************************************************
 * Copyright (c) 2015 Jonas Schnelli                                  *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <btc/bip32.h>

#include "utest.h"
#include <btc/utils.h>

void test_bip32()
{
    btc_hdnode node, node2, node3, node4;
    char str[112];
    int r;
    uint8_t private_key_master[32];
    uint8_t chain_code_master[32];

    /* init m */
    btc_hdnode_from_seed(utils_hex_to_uint8("000102030405060708090a0b0c0d0e0f"), 16, &node);

    /* [Chain m] */
    memcpy(private_key_master,
           utils_hex_to_uint8("e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35"),
           32);
    memcpy(chain_code_master,
           utils_hex_to_uint8("873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508"),
           32);
    u_assert_int_eq(node.fingerprint, 0x00000000);
    u_assert_mem_eq(node.chain_code, chain_code_master, 32);
    u_assert_mem_eq(node.private_key, private_key_master, 32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));

    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "15mKKb2eos1hWa6tisdPwwDC1a5J1y9nma");

    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));


    /* [Chain m/0'] */
    char path0[] = "m/0'";
    btc_hd_generate_key(&node, path0, private_key_master, chain_code_master, false);
    u_assert_int_eq(node.fingerprint, 0x3442193e);
    u_assert_mem_eq(node.chain_code,
                    utils_hex_to_uint8("47fdacbd0f1097043b78c63c20c34ef4ed9a111d980047ad16282c7ae6236141"),
                    32);
    u_assert_mem_eq(node.private_key,
                    utils_hex_to_uint8("edb2e14f9ee77d26dd93b4ecede8d16ed408ce149b6cd80b0715a2d911a0afea"),
                    32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("035a784662a4a20a65bf6aab9ae98a6c068a81c52e4b032c0fb5400c706cfccc56"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ngLNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));

    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "19Q2WoS5hSS6T8GjhK8KZLMgmWaq4neXrh");

    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));


    /* [Chain m/0'/1] */
    char path1[] = "m/0'/1";
    btc_hd_generate_key(&node, path1, private_key_master, chain_code_master, false);
    u_assert_int_eq(node.fingerprint, 0x5c1bd648);
    u_assert_mem_eq(node.chain_code,
                    utils_hex_to_uint8("2a7857631386ba23dacac34180dd1983734e444fdbf774041578e9b6adb37c19"),
                    32);
    u_assert_mem_eq(node.private_key,
                    utils_hex_to_uint8("3c6cb8d0f6a264c91ea8b5030fadaa8e538b020f0a387421a12de9319dc93368"),
                    32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("03501e454bf00751f24b1b489aa925215d66af2234e3891c3b21a52bedb3cd711c"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xprv9wTYmMFdV23N2TdNG573QoEsfRrWKQgWeibmLntzniatZvR9BmLnvSxqu53Kw1UmYPxLgboyZQaXwTCg8MSY3H2EU4pWcQDnRnrVA1xe8fs");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));
    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "1JQheacLPdM5ySCkrZkV66G2ApAXe1mqLj");
    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub6ASuArnXKPbfEwhqN6e3mwBcDTgzisQN1wXN9BJcM47sSikHjJf3UFHKkNAWbWMiGj7Wf5uMash7SyYq527Hqck2AxYysAA7xmALppuCkwQ");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));

    /* [Chain m/0'/1/2'] */
    char path2[] = "m/0'/1/2'";
    btc_hd_generate_key(&node, path2, private_key_master, chain_code_master, false);
    u_assert_int_eq(node.fingerprint, 0xbef5a2f9);
    u_assert_mem_eq(node.chain_code,
                    utils_hex_to_uint8("04466b9cc8e161e966409ca52986c584f07e9dc81f735db683c3ff6ec7b1503f"),
                    32);
    u_assert_mem_eq(node.private_key,
                    utils_hex_to_uint8("cbce0d719ecf7431d88e6a89fa1483e02e35092af60c042b1df2ff59fa424dca"),
                    32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("0357bfe1e341d01c69fe5654309956cbea516822fba8a601743a012a7896ee8dc2"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xprv9z4pot5VBttmtdRTWfWQmoH1taj2axGVzFqSb8C9xaxKymcFzXBDptWmT7FwuEzG3ryjH4ktypQSAewRiNMjANTtpgP4mLTj34bhnZX7UiM");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));
    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "1NjxqbA9aZWnh17q1UW3rB4EPu79wDXj7x");
    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub6D4BDPcP2GT577Vvch3R8wDkScZWzQzMMUm3PWbmWvVJrZwQY4VUNgqFJPMM3No2dFDFGTsxxpG5uJh7n7epu4trkrX7x7DogT5Uv6fcLW5");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));

    /* [Chain m/0'/1/2'/2] */
    char path3[] = "m/0'/1/2'/2";
    btc_hd_generate_key(&node, path3, private_key_master, chain_code_master, false);
    u_assert_int_eq(node.fingerprint, 0xee7ab90c);
    u_assert_mem_eq(node.chain_code,
                    utils_hex_to_uint8("cfb71883f01676f587d023cc53a35bc7f88f724b1f8c2892ac1275ac822a3edd"),
                    32);
    u_assert_mem_eq(node.private_key,
                    utils_hex_to_uint8("0f479245fb19a38a1954c5c7c0ebab2f9bdfd96a17563ef28a6a4b1a2a764ef4"),
                    32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("02e8445082a72f29b75ca48748a914df60622a609cacfce8ed0e35804560741d29"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));
    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "1LjmJcdPnDHhNTUgrWyhLGnRDKxQjoxAgt");
    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));

    /* [Chain m/0'/1/2'/2/1000000000] */
    char path4[] = "m/0'/1/2'/2/1000000000";
    btc_hd_generate_key(&node, path4, private_key_master, chain_code_master, false);
    u_assert_int_eq(node.fingerprint, 0xd880d7d8);
    u_assert_mem_eq(node.chain_code,
                    utils_hex_to_uint8("c783e67b921d2beb8f6b389cc646d7263b4145701dadd2161548a8b078e65e9e"),
                    32);
    u_assert_mem_eq(node.private_key,
                    utils_hex_to_uint8("471b76e389e528d6de6d816857e012c5455051cad6660850e58372a6c3e6e7c8"),
                    32);
    u_assert_mem_eq(node.public_key,
                    utils_hex_to_uint8("022a471424da5e657499d1ff51cb43c47481a03b1e77f951fe64cec9f5a48f7011"),
                    33);
    btc_hdnode_serialize_private(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "xprvA41z7zogVVwxVSgdKUHDy1SKmdb533PjDz7J6N6mV6uS3ze1ai8FHa8kmHScGpWmj4WggLyQjgPie1rFSruoUihUZREPSL39UNdE3BBDu76");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    u_assert_mem_eq(&node, &node2, sizeof(btc_hdnode));
    btc_hdnode_get_p2pkh_address(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "1LZiqrop2HGR4qrH1ULZPyBpU6AUP49Uam");
    btc_hdnode_serialize_public(&node, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str,
                    "xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy");
    r = btc_hdnode_deserialize(str, &btc_chainparams_main, &node2);
    u_assert_int_eq(r, true);
    memcpy(&node3, &node, sizeof(btc_hdnode));
    memset(&node3.private_key, 0, 32);
    u_assert_mem_eq(&node2, &node3, sizeof(btc_hdnode));


    char str_pub_ckd[] = "xpub6LTiQPFh8tFrK56BQXuYcyam39cTXsBvudjQ7NM4EyRABPKapbm9dNe7aYQ6VNDzYHmhZYde5Pv8a6vTeQcfG3g1s7S8g1otXsK8d4qGyLs";

    r = btc_hdnode_deserialize(str_pub_ckd, &btc_chainparams_main, &node4);
    r = btc_hdnode_public_ckd(&node4, 123);
    u_assert_int_eq(r, true);
    btc_hdnode_serialize_public(&node4, &btc_chainparams_main, str, sizeof(str));
    u_assert_str_eq(str, "xpub6Mf5jT2qB3v8YP8frMBbgQ9L79UF6zXzdYbUSAwzezhEQep8w3GfBrbFGquW7T4PQXvmRh8DFEJFbm6qgsJXmT4FjNgrJL2m6YuKJRbsgUa");


    r = btc_hdnode_public_ckd(&node4, 0x80000000 + 1); //try deriving a hardened key (= must fail)
    u_assert_int_eq(r, false);


    char str_pub_ckd_tn[] = "tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK";

    r = btc_hdnode_deserialize(str_pub_ckd_tn, &btc_chainparams_test, &node4);
    r = btc_hdnode_public_ckd(&node4, 123);
    u_assert_int_eq(r, true);
    btc_hdnode_get_p2pkh_address(&node4, &btc_chainparams_test, str, sizeof(str));
    u_assert_str_eq(str, "mp4VkLBrnetj5LkhyNqtgkBzJwRBqhTbaa");
    size_t size = sizeof(str);
    size_t sizeSmall = 55;
    r = btc_hdnode_get_pub_hex(&node4, str, &sizeSmall);
    u_assert_int_eq(r, false);
    r = btc_hdnode_get_pub_hex(&node4, str, &size);
    u_assert_int_eq(size, 66);
    u_assert_int_eq(r, true);
    u_assert_str_eq(str, "0391a9964e79f39cebf9b59eb2151b500bd462e589682d6ceebe8e15970bfebf8b");
    btc_hdnode_serialize_public(&node4, &btc_chainparams_test, str, sizeof(str));
    u_assert_str_eq(str, "tpubD8MQJFN9LVzG8pktwoQ7ApWWKLfUUhonQkeXe8gqi9tFMtMdC34g6Ntj5K6V1hdzR3to2z7dGnQbXaoZSsFkVky7TFWZjmC9Ez4Gog6ujaD");

    btc_hdnode *nodeheap;
    nodeheap = btc_hdnode_new();
    btc_hdnode *nodeheap_copy = btc_hdnode_copy(nodeheap);

    u_assert_int_eq(memcmp(nodeheap->private_key, nodeheap_copy->private_key, 32), 0);
    u_assert_int_eq(memcmp(nodeheap->public_key, nodeheap_copy->public_key, 33), 0)

    btc_hdnode_free(nodeheap);
    btc_hdnode_free(nodeheap_copy);
}
