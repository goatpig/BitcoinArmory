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

#include <stdint.h>
#include <stddef.h>

#ifndef __LIBLOGDB_BASE_H__
#define __LIBLOGDB_BASE_H__

#define UNUSED(x) (void)(x)

typedef uint8_t logdb_bool; /*!serialize, c/c++ save bool*/

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef NULL
#define NULL 0
#endif 

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIBLOGDB_API
#if defined(_WIN32)
#ifdef LIBBTC_BUILD
#define LIBLOGDB_API __declspec(dllexport)
#else
#define LIBLOGDB_API
#endif
#elif defined(__GNUC__) && defined(LIBBTC_BUILD)
#define LIBLOGDB_API __attribute__((visibility("default")))
#else
#define LIBLOGDB_API
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LIBLOGDB_BASE_H__ */
