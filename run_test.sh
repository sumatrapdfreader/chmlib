#!/bin/bash

if [ -e /Volumes/Store ]; then
  go run tools/test_dir.go /Volumes/Store
fi

if [ -e ~/Downloads/chmdocs ]; then
  go run tools/test_dir.go ~/Downloads/chmdocs
fi
