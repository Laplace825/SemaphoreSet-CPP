#!/usr/bin/bash

IN=0

mkdir -p cmake-build
cmake -B cmake-build -S . -G Ninja
ninja -C cmake-build -j 8 -v

ok=`echo $?`


if [ $ok == 0 ]; then

while [ $IN -lt 10 ];
do
    IN=$((IN+1))
    echo -e "\033[1;33m========Running $IN=========\033[0m"
    read 
    ./cmake-build/SHU_OS_DIS_2
    read 
done

fi
