CC = gcc
CFLAGS = -Og -Wall -Wextra -pedantic -std=c99 -D_FORTIFY_SOURCE=2 -D_XOPEN_SOURCE=700
LDLIBS = -lpthread

FILES = empty_test

.PHONY: all
all: $(FILES)

empty_test: empty_test.o csapp.o

.PHONY: format
format: csapp.c csapp.h
	$(LLVM_PATH)clang-format -style=file -i $^

.PHONY: clean
clean:
	rm -f *.o $(FILES)
