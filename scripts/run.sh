#!/usr/bin/bash

IN=0
while [ $IN -lt 10 ];
do
    IN=$((IN+1))
    echo -e "\033[1;33m========Running $IN=========\033[0m"
    read 
    ./cmake-build/SHU_OS_DIS_2
    read 
done