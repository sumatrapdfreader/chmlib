#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

source ./build_common.sh

build_afl

if [ -e ~/Downloads/chm_fuzz_files ]; then
  afl-fuzz -i ~/Downloads/chm_fuzz_files -o afl_findings_dir obj/afl/rel/test @@
fi
