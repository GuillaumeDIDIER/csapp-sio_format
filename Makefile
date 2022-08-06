CC = $(LLVM_PATH)clang
CFLAGS = -Og -g -Wall -Wextra -pedantic -std=c99 -D_FORTIFY_SOURCE=2 -D_XOPEN_SOURCE=700 \
         -Weverything -Wno-disabled-macro-expansion -Wno-padded
LDLIBS = -lpthread

LLVM_PATH = /usr/local/depot/llvm-7.0/bin/
ifneq (,$(wildcard /usr/lib/llvm-7/bin/))
  LLVM_PATH = /usr/lib/llvm-7/bin/
endif

FILES = empty_test test_sio_assert test_sio_printf

.PHONY: all
all: $(FILES)

empty_test: empty_test.o csapp.o
test_sio_assert: test_sio_assert.o csapp.o
test_sio_printf: test_sio_printf.o csapp.o
test_sio_snprintf: test_sio_snprintf.o csapp.o

.PHONY: format
format: csapp.c csapp.h
	$(LLVM_PATH)clang-format -style=file -i $^

.PHONY: clean
clean:
	rm -f *.o $(FILES)
