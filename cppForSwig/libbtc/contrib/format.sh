#!/bin/bash

echo "execute from git root dir"
find . \( -not -path './src/secp256k1/*' -and \( -name '*.h' -or -name '*.c' \) \) -prune -print0 | xargs -0 "clang-format" -i
