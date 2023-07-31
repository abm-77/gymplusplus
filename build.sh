#!/bin/bash

clear
echo "Building..."

if [ ! -d ./bld ]
then
    mkdir ./bld
fi

cd ./bld

gcc -std=c99 -g -o ../bin/application ../src/main.c -lsqlite3

cd ..

echo "Running..."

./bin/application

echo "Done!"
