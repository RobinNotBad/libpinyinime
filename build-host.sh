#!/usr/bin/env bash
set -e

CXX="${CXX:-g++}"

$CXX -O2 -std=c++11 -fPIC -shared -Iinclude \
    src/pinyin_ime.cpp src/utf8.cpp \
    -o libpinyinime.so

# 编译 C 调用示例
CC="${CC:-gcc}"
$CC -Iinclude examples/test.c -L. -Wl,-rpath,'$ORIGIN' -lpinyinime -o example_test

echo "构建完成: libpinyinime.so, example_test"
