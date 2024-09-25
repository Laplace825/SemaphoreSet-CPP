#!/usr/bin/bash

# Build the project

mkdir -p cmake-build

cmake -S . -B cmake-build -G Ninja
ninja -C cmake-build -j 8

ok=$?

if [ $ok -eq 0 ]; then
    echo -e "\033[1;32mBuild successful\033[0m"
else
    echo -e "\033[1;31mBuild failed\033[0m"
fi