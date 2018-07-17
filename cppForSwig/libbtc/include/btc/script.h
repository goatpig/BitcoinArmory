/*

 The MIT License (MIT)

 Copyright (c) 2015 Jonas Schnelli

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

#ifndef __LIBBTC_SCRIPT_H__
#define __LIBBTC_SCRIPT_H__

#include "btc.h"
#include "cstr.h"
#include "ecc_key.h"
#include "vector.h"

LIBBTC_BEGIN_DECL

/** Sighash version types */
enum btc_sig_version
{
    SIGVERSION_BASE = 0,
    SIGVERSION_WITNESS_V0 = 1,
};

/** Signature hash types/flags */
enum {
    SIGHASH_ALL = 1,
    SIGHASH_NONE = 2,
    SIGHASH_SINGLE = 3,
    SIGHASH_ANYONECANPAY = 0x80,
};

/** Script opcodes */
enum opcodetype {
    // push value
    OP_0 = 0x00,
    OP_FALSE = OP_0,
    OP_PUSHDATA1 = 0x4c,
    OP_PUSHDATA2 = 0x4d,
    OP_PUSHDATA4 = 0x4e,
    OP_1NEGATE = 0x4f,
    OP_RESERVED = 0x50,
    OP_1 = 0x51,
    OP_TRUE = OP_1,
    OP_2 = 0x52,
    OP_3 = 0x53,
    OP_4 = 0x54,
    OP_5 = 0x55,
    OP_6 = 0x56,
    OP_7 = 0x57,
    OP_8 = 0x58,
    OP_9 = 0x59,
    OP_10 = 0x5a,
    OP_11 = 0x5b,
    OP_12 = 0x5c,
    OP_13 = 0x5d,
    OP_14 = 0x5e,
    OP_15 = 0x5f,
    OP_16 = 0x60,

    // control
    OP_NOP = 0x61,
    OP_VER = 0x62,
    OP_IF = 0x63,
    OP_NOTIF = 0x64,
    OP_VERIF = 0x65,
    OP_VERNOTIF = 0x66,
    OP_ELSE = 0x67,
    OP_ENDIF = 0x68,
    OP_VERIFY = 0x69,
    OP_RETURN = 0x6a,

    // stack ops
    OP_TOALTSTACK = 0x6b,
    OP_FROMALTSTACK = 0x6c,
    OP_2DROP = 0x6d,
    OP_2DUP = 0x6e,
    OP_3DUP = 0x6f,
    OP_2OVER = 0x70,
    OP_2ROT = 0x71,
    OP_2SWAP = 0x72,
    OP_IFDUP = 0x73,
    OP_DEPTH = 0x74,
    OP_DROP = 0x75,
    OP_DUP = 0x76,
    OP_NIP = 0x77,
    OP_OVER = 0x78,
    OP_PICK = 0x79,
    OP_ROLL = 0x7a,
    OP_ROT = 0x7b,
    OP_SWAP = 0x7c,
    OP_TUCK = 0x7d,

    // splice ops
    OP_CAT = 0x7e,
    OP_SUBSTR = 0x7f,
    OP_LEFT = 0x80,
    OP_RIGHT = 0x81,
    OP_SIZE = 0x82,

    // bit logic
    OP_INVERT = 0x83,
    OP_AND = 0x84,
    OP_OR = 0x85,
    OP_XOR = 0x86,
    OP_EQUAL = 0x87,
    OP_EQUALVERIFY = 0x88,
    OP_RESERVED1 = 0x89,
    OP_RESERVED2 = 0x8a,

    // numeric
    OP_1ADD = 0x8b,
    OP_1SUB = 0x8c,
    OP_2MUL = 0x8d,
    OP_2DIV = 0x8e,
    OP_NEGATE = 0x8f,
    OP_ABS = 0x90,
    OP_NOT = 0x91,
    OP_0NOTEQUAL = 0x92,

    OP_ADD = 0x93,
    OP_SUB = 0x94,
    OP_MUL = 0x95,
    OP_DIV = 0x96,
    OP_MOD = 0x97,
    OP_LSHIFT = 0x98,
    OP_RSHIFT = 0x99,

    OP_BOOLAND = 0x9a,
    OP_BOOLOR = 0x9b,
    OP_NUMEQUAL = 0x9c,
    OP_NUMEQUALVERIFY = 0x9d,
    OP_NUMNOTEQUAL = 0x9e,
    OP_LESSTHAN = 0x9f,
    OP_GREATERTHAN = 0xa0,
    OP_LESSTHANOREQUAL = 0xa1,
    OP_GREATERTHANOREQUAL = 0xa2,
    OP_MIN = 0xa3,
    OP_MAX = 0xa4,

    OP_WITHIN = 0xa5,

    // crypto
    OP_RIPEMD160 = 0xa6,
    OP_SHA1 = 0xa7,
    OP_SHA256 = 0xa8,
    OP_HASH160 = 0xa9,
    OP_HASH256 = 0xaa,
    OP_CODESEPARATOR = 0xab,
    OP_CHECKSIG = 0xac,
    OP_CHECKSIGVERIFY = 0xad,
    OP_CHECKMULTISIG = 0xae,
    OP_CHECKMULTISIGVERIFY = 0xaf,

    // expansion
    OP_NOP1 = 0xb0,
    OP_NOP2 = 0xb1,
    OP_CHECKLOCKTIMEVERIFY = OP_NOP2,
    OP_NOP3 = 0xb2,
    OP_NOP4 = 0xb3,
    OP_NOP5 = 0xb4,
    OP_NOP6 = 0xb5,
    OP_NOP7 = 0xb6,
    OP_NOP8 = 0xb7,
    OP_NOP9 = 0xb8,
    OP_NOP10 = 0xb9,


    // template matching params
    OP_SMALLINTEGER = 0xfa,
    OP_PUBKEYS = 0xfb,
    OP_PUBKEYHASH = 0xfd,
    OP_PUBKEY = 0xfe,

    OP_INVALIDOPCODE = 0xff,
};

enum btc_tx_out_type {
    BTC_TX_INVALID = -1,
    BTC_TX_NONSTANDARD,
    // 'standard' transaction types:
    BTC_TX_PUBKEY,
    BTC_TX_PUBKEYHASH,
    BTC_TX_SCRIPTHASH,
    BTC_TX_MULTISIG,
    BTC_TX_WITNESS_V0_PUBKEYHASH,
    BTC_TX_WITNESS_V0_SCRIPTHASH,
};

typedef struct btc_script_op_ {
    enum opcodetype op;  /* opcode found */
    unsigned char* data; /* associated data, if any */
    size_t datalen;
} btc_script_op;

// Maximum script length in bytes
static const int MAX_SCRIPT_SIZE = 10000;

//copy a script without the codeseperator ops
btc_bool btc_script_copy_without_op_codeseperator(const cstring* scriptin, cstring* scriptout);

LIBBTC_API btc_script_op* btc_script_op_new();
LIBBTC_API void btc_script_op_free(btc_script_op* script_op);
void btc_script_op_free_cb(void* data);
btc_bool btc_script_get_ops(const cstring* script_in, vector* ops_out);

LIBBTC_API enum btc_tx_out_type btc_script_classify_ops(const vector* ops);
LIBBTC_API enum btc_tx_out_type btc_script_classify(const cstring* script, vector* data_out);

LIBBTC_API enum opcodetype btc_encode_op_n(const int n);
LIBBTC_API void btc_script_append_op(cstring* script_in, enum opcodetype op);
LIBBTC_API void btc_script_append_pushdata(cstring* script_in, const unsigned char* data, const size_t datalen);

LIBBTC_API btc_bool btc_script_build_multisig(cstring* script_in, const unsigned int required_signatures, const vector* pubkeys_chars);
LIBBTC_API btc_bool btc_script_build_p2pkh(cstring* script, const uint160 hash160);
LIBBTC_API btc_bool btc_script_build_p2wpkh(cstring* script, const uint160 hash160);
LIBBTC_API btc_bool btc_script_build_p2sh(cstring* script_in, const uint160 hash160);
LIBBTC_API btc_bool btc_script_get_scripthash(const cstring* script_in, uint160 scripthash);

LIBBTC_API const char * btc_tx_out_type_to_str(const enum btc_tx_out_type type);

LIBBTC_API btc_bool btc_script_is_witnessprogram(const cstring* script, uint8_t* version_out, uint8_t *program_out, int *programm_len_out);

LIBBTC_END_DECL

#endif // __LIBBTC_SCRIPT_H__
