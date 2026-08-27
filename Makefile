CXXFLAGS ?= -O2 -std=c++11 -fPIC -Wall -Wextra
CFLAGS   ?= -O2 -Wall -Wextra
INCLUDES  = -Iinclude
SRC       = src/pinyin_ime.cpp src/utf8.cpp
LIB       = libpinyinime.so

.PHONY: all example cli clean

all: $(LIB) example

$(LIB): $(SRC) include/pinyin_ime.h src/utf8.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared $(SRC) -o $(LIB)

example: examples/test.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) examples/test.c -L. -Wl,-rpath,'$$ORIGIN' -lpinyinime -o example_test

clean:
	rm -f $(LIB) example_test
