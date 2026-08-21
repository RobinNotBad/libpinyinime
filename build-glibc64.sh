#!/usr/bin/env bash
set -e

export TOOLCHAIN=$HOME/toolchain-glibc
export PATH=$PATH:$TOOLCHAIN/bin
export CC=$TOOLCHAIN/bin/aarch64-openwrt-linux-gcc
export CXX=$TOOLCHAIN/bin/aarch64-openwrt-linux-g++
export AR=$TOOLCHAIN/bin/aarch64-openwrt-linux-ar
export RANLIB=$TOOLCHAIN/bin/aarch64-openwrt-linux-ranlib
export STRIP=$TOOLCHAIN/bin/aarch64-openwrt-linux-strip

export STAGING_DIR=$TOOLCHAIN/bin

$CXX -O2 -std=c++11 -fPIC -shared -Iinclude \
    src/pinyin_ime.cpp src/utf8.cpp \
    -o libpinyinime.so

$CC -Iinclude examples/test.c -L. -L$HOME/toolchain-glibc/lib -Wl,-rpath-link=$HOME/toolchain-glibc/lib -Wl,--disable-new-dtags -lpinyinime -o example_test

echo "构建完成: libpinyinime.so, example_test"
