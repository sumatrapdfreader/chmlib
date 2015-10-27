#!/bin/bash

set -o nounset
set -o errexit
set -o pipefail

./build.sh

if [ -e /Volumes/Store ]; then
  go run tools/test_dir.go -check-ref /Volumes/Store
fi

if [ -e ~/Downloads/chmdocs ]; then
  go run tools/test_dir.go -check-ref ~/Downloads/chmdocs
fi
