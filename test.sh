#!/bin/bash

if [ ! -f "./sender" ] ; then
	echo "file not exist"
	exit 0
fi

for i in {1..100000..1}
do
	./sender 444444444444444444444444444444444444444444444444
done

