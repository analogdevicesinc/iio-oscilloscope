#!/bin/bash

# Execute the fixer script (which is should be passed as the first argument)
# for all glade files in this repo

if [ $1 = "" ]; then
        echo "You need to pass a fixer"
        exit
fi

for file in ../glade/*.glade; do
        eval "./$1 $file"
done
