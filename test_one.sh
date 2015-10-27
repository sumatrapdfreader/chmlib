#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

./build.sh

obj/clang/rel/test /Volumes/Store/books/_chm/XMLPROBZ.CHM
