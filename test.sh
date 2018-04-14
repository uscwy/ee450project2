#!/bin/bash

if [ ! -f "./sender" ] ; then
	echo "file not exist"
	exit 0
fi
#send 1000 bytes
openssl rand -hex 500 | ./sender
