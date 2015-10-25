#!/bin/bash

# -style=file : read style from .clang-format
# -i : edit in-place
clang-format -style=file -i src/*.c src/*.h
clang-format -style=file -i tools/*.c
