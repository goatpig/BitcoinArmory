/*

 Copyright (c) 2015 Douglas J. Bakkum

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


/*

//
// Macros for light weight unit testing based on
// minunit.h from http://www.jera.com/techinfo/jtns/jtn002.html
//
// Example code:
//

#include "utest.h"

 int U_TESTS_RUN = 0;
 int U_TESTS_FAIL = 0;

 static void test_str(void) {
     char * str = "hello";
     u_assert_str_eq(str, "hello");
     u_assert_str_eq(str, "hellooo");
     return;
 }

 static void test_int(void) {
    int i = 7;
    u_assert_int_eq(i, 7);
    u_assert_int_eq(i, 8);
    return;
 }

 static void test_mem(void) {
     char * str = "hello";
     u_assert_mem_eq(str, "hello", 5);
     u_assert_mem_eq(str, "hellooo", 5);
     u_assert_mem_eq(str, "helooo", 5);
     return;
 }


 int main(void)
 {
     u_run_test(test_str);
     u_run_test(test_mem);
     u_run_test(test_int);
     if (!U_TESTS_FAIL) {
        printf("\nALL TESTS PASSED\n\n");
     }
     return U_TESTS_FAIL;
 }
*/


#ifndef _UTEST_H_
#define _UTEST_H_
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define u_run_test(TEST)                      \
    do {                                      \
        int f_ = U_TESTS_FAIL;                \
        TEST();                               \
        U_TESTS_RUN++;                        \
        if (f_ == U_TESTS_FAIL) {             \
            printf("PASSED - %s()\n", #TEST); \
        };                                    \
    } while (0)

#define u_assert_int_eq(R, E)                                            \
    {                                                                    \
        int r_ = (R);                                                    \
        int e_ = (E);                                                    \
        do {                                                             \
            if (r_ != e_) {                                              \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \t%d\n", e_);                          \
                printf("\tReceive:\t%d\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_uint32_eq(R, E)                                         \
    {                                                                    \
        uint64_t r_ = (R);                                               \
        uint64_t e_ = (E);                                               \
        do {                                                             \
            if (r_ != e_) {                                              \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \t%" PRIu64 "\n", e_);                        \
                printf("\tReceive:\t%" PRIu64 "\n", r_);                        \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_str_eq(R, E)                                            \
    {                                                                    \
        const char* r_ = (R);                                            \
        const char* e_ = (E);                                            \
        do {                                                             \
            if (strcmp(r_, e_)) {                                        \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \t%s\n", e_);                          \
                printf("\tReceive:\t%s\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_str_not_eq(R, E)                                        \
    {                                                                    \
        const char* r_ = (R);                                            \
        const char* e_ = (E);                                            \
        do {                                                             \
            if (!strcmp(r_, e_)) {                                       \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tNot expect:\t%s\n", e_);                       \
                printf("\tReceive:\t%s\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_str_has(R, E)                                           \
    {                                                                    \
        const char* r_ = (R);                                            \
        const char* e_ = (E);                                            \
        do {                                                             \
            if (!strstr(r_, e_)) {                                       \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \t%s\n", e_);                          \
                printf("\tReceive:\t%s\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_str_has_not(R, E)                                       \
    {                                                                    \
        const char* r_ = (R);                                            \
        const char* e_ = (E);                                            \
        do {                                                             \
            if (strstr(r_, e_)) {                                        \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tNot expect:\t%s\n", e_);                       \
                printf("\tReceive:\t%s\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_mem_eq(R, E, L)                                         \
    {                                                                    \
        const void* r_ = (R);                                            \
        const void* e_ = (E);                                            \
        size_t l_ = (L);                                                 \
        do {                                                             \
            if (memcmp(r_, e_, l_)) {                                    \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \t%s\n", utils_uint8_to_hex(e_, l_));  \
                printf("\tReceive:\t%s\n", utils_uint8_to_hex(r_, l_));  \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_is_null(R)                                              \
    {                                                                    \
        const void* r_ = (R);                                            \
        do {                                                             \
            if (r_) {                                                    \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \tNULL\n");                            \
                printf("\tReceive:\t%p\n", r_);                          \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

#define u_assert_not_null(R)                                             \
    {                                                                    \
        const void* r_ = (R);                                            \
        do {                                                             \
            if (!r_) {                                                   \
                printf("FAILED - %s() - Line %d\n", __func__, __LINE__); \
                printf("\tExpect: \tnot NULL\n");                        \
                printf("\tReceive:\tNULL\n", r_);                        \
                U_TESTS_FAIL++;                                          \
                return;                                                  \
            };                                                           \
        } while (0);                                                     \
    }

extern int U_TESTS_RUN;
extern int U_TESTS_FAIL;

#endif
