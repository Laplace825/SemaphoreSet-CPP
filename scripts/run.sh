#!/usr/bin/bash

IN=0
while [ $IN -lt 10 ];
do
    IN=$((IN+1))
    echo -e "\033[1;33m========Running $IN=========\033[0m"
    read 
    ./bin/SemaphoreSet
    read 
done