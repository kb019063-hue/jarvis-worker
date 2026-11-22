#!/bin/sh
# Run after `make build/litebird` with the default build directory.
set -eu
cd "$(dirname "$0")/../../.."
probe=$(mktemp)
trap 'rm -f "$probe"' EXIT
${CXX:-c++} -std=c++20 -Iinclude tools/real_pages/dialog-04/check.cpp \
    $(find build -mindepth 2 -name '*.o') -o "$probe"
"$probe" "file://$(pwd)/tools/real_pages/dialog-04/backdrop.html"
