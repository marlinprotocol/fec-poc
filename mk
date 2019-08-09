#!/bin/sh

mkdir -p build
cd build
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/roma/data/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_INSTALL_PREFIX=INST \
    ..
make
