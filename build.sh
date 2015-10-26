#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

## Available defines for building chm_lib with particular options
# CHM_MT:        build thread-safe version of chm_lib
# CHM_USE_PREAD: build chm_lib to use pread/pread64 for all I/O
# CHM_USE_IO64:  build chm_lib to support 64-bit file I/O
#
# Note: LDFLAGS must contain -lpthread if you are using -DCHM_MT.
#
#CFLAGS=-DCHM_MT -DCHM_USE_PREAD -DCHM_USE_IO64
#CFLAGS=-DCHM_MT -DCHM_USE_PREAD -DCHM_USE_IO64 -g -DDMALLOC_DISABLE
#LDFLAGS=-lpthread

CHM_SRCS="src/chm_lib.c src/lzx.c"

clang_rel()
{
  CC=clang
  CFLAGS="-g -fsanitize=address -O3 -Isrc -Wall"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test_chmLib $CFLAGS $CHM_SRCS tools/test_chmLib.c
  $CC -o $OUT/extract_chmLib $CFLAGS $CHM_SRCS tools/extract_chmLib.c
  $CC -o $OUT/enumdir_chmLib $CFLAGS $CHM_SRCS tools/enumdir_chmLib.c
  $CC -o $OUT/enum_chmLib $CFLAGS $CHM_SRCS tools/enum_chmLib.c
  $CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

clang_rel_one()
{
  CC=clang
  CFLAGS="-g -fsanitize=address -O3 -Isrc -Weverything -Wno-sign-conversion -Wno-padded -Wno-sign-compare -Wno-conversion"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/extract_chmLib $CFLAGS $CHM_SRCS tools/extract_chmLib.c
}

clang_dbg()
{
  CC=clang
  CFLAGS="-g -fsanitize=address -O0 -Isrc -Wall"
  OUT=obj/clang/dbg
  mkdir -p $OUT
  $CC -o $OUT/test_chmLib $CFLAGS $CHM_SRCS tools/test_chmLib.c
  $CC -o $OUT/extract_chmLib $CFLAGS $CHM_SRCS tools/extract_chmLib.c
  $CC -o $OUT/enumdir_chmLib $CFLAGS $CHM_SRCS tools/enumdir_chmLib.c
  $CC -o $OUT/enum_chmLib $CFLAGS $CHM_SRCS tools/enum_chmLib.c
  $CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

gcc_rel()
{
  #CC=/usr/local/opt/gcc/bin/gcc-5
  CC=gcc-5 # this is on mac when installed with brew install gcc
  CFLAGS="-g -O3 -Isrc -Wall"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test_chmLib $CFLAGS $CHM_SRCS tools/test_chmLib.c
  $CC -o $OUT/extract_chmLib $CFLAGS $CHM_SRCS tools/extract_chmLib.c
  $CC -o $OUT/enumdir_chmLib $CFLAGS $CHM_SRCS tools/enumdir_chmLib.c
  $CC -o $OUT/enum_chmLib $CFLAGS $CHM_SRCS tools/enum_chmLib.c
  $CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

#gcc_rel
clang_rel_one
