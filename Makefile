CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -Wvla -Wstrict-prototypes -Wno-switch -fwrapv
SRCS=buffer.c file.c string_pool.c hash_table.c cpp.c token.c lex.c main.c
OBJS=$(SRCS:.c=.o)

ifdef DEBUG
	CFLAGS+=-g -Og
	CFLAGS+=-DXXH_NO_INLINE_HINTS=1
ifeq ($(DEBUG),asan)
	CFLAGS+=-fsanitize=address,undefined
endif
else
	CFLAGS+=-O2
endif

cpp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	mkdir -p build
	mv *.o cpp build

clean:
	rm -rf build

.PHONY: clean
