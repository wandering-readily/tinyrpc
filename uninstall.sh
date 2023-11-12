#!/bin/bash

sudo rm -rf /usr/include/tinyrpc/ && sudo rm -rf /usr/lib/libtinyrpc.a
if [ -d "./log/" ];then
    sudo rm -rf ./log/*
fi