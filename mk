#!/bin/sh

# Prerequisites:
# - vcpkg.cmake with one line:
#     include(/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake NO_POLICY_SCOPE)
# - Boost installed via vcpkg

cd "$(dirname "$0")"

mkdir -p build
cmake -B build -DCMAKE_INSTALL_PREFIX=INST -DCMAKE_TOOLCHAIN_FILE=vcpkg.cmake
(cd build && make)
