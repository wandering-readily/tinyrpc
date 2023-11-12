#!/bin/bash

set -x

if [ ! -d "./bin/" ];then
    mkdir bin 
fi

if [ ! -d "./lib/" ];then
    mkdir lib
fi

if [ ! -d "./log/" ];then
    mkdir log
    chmod a+rw log
fi

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-./build}
BIN_DIR=${BIN_DIR:-./bin}
LIB_DIR=${LIB_DIR:-./lib}

PATH_INSTALL_INC_ROOT=${PATH_INSTALL_INC_ROOT:-/usr/include}
PATH_INSTALL_LIB_ROOT=${PATH_INSTALL_LIB_ROOT:-/usr/lib}
INCLUDE_DIR=${INCLUDE_DIR:-./include}
LIB=${LIB:-./lib/libtinyrpc.a}

mkdir -p ${INCLUDE_DIR} \
    && cd ${BUILD_DIR} \
    && cmake .. \
    && make install \
    && cd .. \
    && cp -r ${INCLUDE_DIR}/tinyrpc ${PATH_INSTALL_INC_ROOT} \
    && cp ${LIB} ${PATH_INSTALL_LIB_ROOT} \
    && rm -rf ${INCLUDE_DIR}

