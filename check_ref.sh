#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

CHM_SRCS="src/chm_lib.c src/lzx.c"

clang_rel_one()
{
  CC=clang
  # ASAN only seems to work with -O0 (didn't trigger when I compiled with -O1, -O2 and -O3
  CFLAGS="-g -fsanitize=address -O0 -Isrc -Weverything -Wno-padded -Wno-conversion"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test $CFLAGS $CHM_SRCS tools/test.c tools/sha1.c
}

clang_rel_one

if [ -e /Volumes/Store ]; then
  go run tools/test_dir.go -check-ref /Volumes/Store
fi

if [ -e ~/Downloads/chmdocs ]; then
  go run tools/test_dir.go -check-ref ~/Downloads/chmdocs
fi
