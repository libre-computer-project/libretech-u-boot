#!/bin/bash

cd $(readlink -f $(dirname ${BASH_SOURCE[0]}))

TARGET=cacert.crt
wget -O $TARGET "https://curl.se/ca/cacert.pem"
echo -en "\0" >> $TARGET
