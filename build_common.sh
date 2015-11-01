#!/bin/bash

## Available defines for building chm_lib with particular options
# CHM_USE_PREAD: build chm_lib to use pread/pread64 for all I/O
# CHM_USE_IO64:  build chm_lib to support 64-bit file I/O
#
#CFLAGS=-DCHM_USE_PREAD -DCHM_USE_IO64
#CFLAGS=-DCHM_USE_PREAD -DCHM_USE_IO64 -g -DDMALLOC_DISABLE
#LDFLAGS=-lpthread

CHM_SRCS="src/chm_lib.c src/lzx.c"

clang_rel()
{
  CC=clang
  # ASAN only seems to work with -O0 (didn't trigger when I compiled with -O1, -O2 and -O3
  CFLAGS="-g -fsanitize=address -O0 -Isrc -Weverything -Wno-format-nonliteral -Wno-padded -Wno-conversion"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test $CFLAGS $CHM_SRCS tools/test.c tools/sha1.c
  $CC -o $OUT/extract $CFLAGS $CHM_SRCS tools/extract.c
  $CC -o $OUT/enum $CFLAGS $CHM_SRCS tools/enum.c
  #$CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

clang_rel_one()
{
  CC=clang
  # ASAN only seems to work with -O0 (didn't trigger when I compiled with -O1, -O2 and -O3
  CFLAGS="-g -fsanitize=address -O0 -Isrc -Weverything -Wno-format-nonliteral -Wno-padded -Wno-conversion"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test $CFLAGS $CHM_SRCS tools/test.c tools/sha1.c
  #$CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

clang_dbg()
{
  CC=clang
  CFLAGS="-g -fsanitize=address -O0 -Isrc -Weverything -Wno-format-nonliteral -Wno-padded -Wno-conversion"
  OUT=obj/clang/dbg
  mkdir -p $OUT
  $CC -o $OUT/test $CFLAGS $CHM_SRCS tools/test.c tools/sha1.c
  $CC -o $OUT/extract $CFLAGS $CHM_SRCS tools/extract.c
  $CC -o $OUT/enum $CFLAGS $CHM_SRCS tools/enum.c
  #$CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}

gcc_rel()
{
  #CC=/usr/local/opt/gcc/bin/gcc-5
  CC=gcc-5 # this is on mac when installed with brew install gcc
  CFLAGS="-g -O3 -Isrc -Wall"
  OUT=obj/clang/rel
  mkdir -p $OUT
  $CC -o $OUT/test $CFLAGS $CHM_SRCS tools/test.c tools/sha1.c
  $CC -o $OUT/extract $CFLAGS $CHM_SRCS tools/extract.c
  $CC -o $OUT/enum $CFLAGS $CHM_SRCS tools/enum.c
  #$CC -o $OUT/chm_http $CFLAGS $CHM_SRCS tools/chm_http.c
}
