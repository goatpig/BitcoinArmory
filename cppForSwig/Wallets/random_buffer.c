////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2024, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <trezor-crypto/rand.h>

/***
This file satisfies the linkage requirement from bip39.c distrubted with libbtc.
We should never use this function (implicit seed generation in bip39.c), as we
always feed explicit seeds.
***/

void random_buffer(uint8_t*, size_t)
{
   fprintf(stderr, "bip39.c never call random_buffer");
   exit(EXIT_FAILURE);
}
