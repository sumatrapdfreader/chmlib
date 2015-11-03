#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

source ./build_common.sh

build_afl

if [ -e  /Volumes/Store/books/_chm ]; then
  afl-fuzz -i  /Volumes/Store/books/_chm -o afl_findings_dir obj/afl/rel/test @@
fi

if [ -e ~/Downloads/_chm ]; then
  afl-fuzz -i ~/Downloads/_chm -o afl_findings_dir obj/afl/rel/test @@
fi
