CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -std=c++11 -fPIC -Wall -Wextra
CFLAGS   ?= -O2 -Wall -Wextra
INCLUDES  = -Iinclude
SRC       = src/pinyin_ime.cpp src/utf8.cpp
LIB       = libpinyinime.so

.PHONY: all example cli clean

all: $(LIB)

$(LIB): $(SRC) include/pinyin_ime.h src/utf8.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared $(SRC) -o $(LIB)

example: examples/test.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) examples/test.c -L. -Wl,-rpath,'$$ORIGIN' -lpinyinime -o example_test

# 原命令行输入法程序（可选）
cli: main.cpp txt2data.cpp
	$(CXX) $(CXXFLAGS) main.cpp txt2data.cpp -o pinyin-ime

clean:
	rm -f $(LIB) example_test
