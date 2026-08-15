#!/usr/bin/env bash
set -e

g++ -O2 -std=c++11 main.cpp txt2data.cpp -o pinyin-ime
