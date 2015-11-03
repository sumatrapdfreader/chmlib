#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

# this is on mac after brew install llvm
SCAN_BUILD=/usr/local/opt/llvm/bin/scan-build
$SCAN_BUILD ./build.sh
