#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

source ./build.sh
clang_rel_one

obj/clang/rel/test /Volumes/Store/books/_chm/XMLPROBZ.CHM
