#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

source ./build.sh
clang_rel_one

if [ -e  /Volumes/Store/books/_chm/XMLPROBZ.CHM ]; then
  obj/clang/rel/test /Volumes/Store/books/_chm/XMLPROBZ.CHM
fi

if [ -e ~/Downloads/chmdocs/DesignSecureWebApps.chm ]; then
  obj/clang/rel/test ~/Downloads/chmdocs/DesignSecureWebApps.chm
fi
