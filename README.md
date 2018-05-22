[![Build Status](https://travis-ci.org/jonasschnelli/chacha20poly1305.svg?branch=master)](https://travis-ci.org/jonasschnelli/chacha20poly1305) 

chacha20/poly1305/chacha20poly1305 openssh aead
=====

Simple C module for chacha20, poly1305 and chacha20poly1305@openssh AEAD

Features:
* Simple, pure C code without any dependencies.

Performance
-----------

-

Build steps
-----------

Object code:

    $ gcc -O3 -c poly1305.c chacha.c chachapoly_aead.c

Tests:

    $ gcc -O3 poly1305.c chacha.c chachapoly_aead.c tests.c -o test

Benchmark:

    $ gcc -O3 poly1305.c chacha.c chachapoly_aead.c bench.c -o bench
    