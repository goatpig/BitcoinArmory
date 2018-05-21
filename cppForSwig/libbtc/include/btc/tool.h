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

#ifndef __LIBBTC_TOOL_H__
#define __LIBBTC_TOOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "btc.h"
#include "tx.h"

#include <stddef.h>
#include <stdint.h>

/* generate the p2pkh address from a given hex pubkey */
LIBBTC_API btc_bool addresses_from_pubkey(const btc_chainparams* chain, const char* pubkey_hex, char* p2pkh_address, char* p2sh_p2wpkh_address, char *p2wpkh_address);

/* generate the hex publickey from a given hex private key */
LIBBTC_API btc_bool pubkey_from_privatekey(const btc_chainparams* chain, const char* privkey_hex, char* pubkey_hex, size_t* sizeout);

/* generate a new private key (hex) */
LIBBTC_API btc_bool gen_privatekey(const btc_chainparams* chain, char* privkey_wif, size_t strsize_wif, char* privkey_hex);

LIBBTC_API btc_bool hd_gen_master(const btc_chainparams* chain, char* masterkeyhex, size_t strsize);
LIBBTC_API btc_bool hd_print_node(const btc_chainparams* chain, const char* nodeser);
LIBBTC_API btc_bool hd_derive(const btc_chainparams* chain, const char* masterkey, const char* keypath, char* extkeyout, size_t extkeyout_size);

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_TOOL_H__
