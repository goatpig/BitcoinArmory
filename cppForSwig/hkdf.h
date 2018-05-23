////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// An HKDF implementation based on the code available with libbtc. For now, the
// code assumes SHA-256 is being used. This functionality is global.

#ifndef HKDF_H
#define HKDF_H

#include <cstdint>

void hkdf_sha256(uint8_t *result, const size_t &resultSize,
                 const uint8_t *salt, const size_t &ssize,
                 const uint8_t *key, const size_t &ksize,
                 const uint8_t *info, const size_t &isize);
#endif // HKDF_H
