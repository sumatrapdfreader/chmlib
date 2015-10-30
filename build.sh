#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

source ./build_common.sh

#gcc_rel
#clang_rel_one
clang_rel
