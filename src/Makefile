CC=gcc
#CFLAGS=-std=c11 -Wall -Wextra -Wvla -Wstrict-prototypes -Wno-switch -fwrapv -g -I/home/nkw/stuff/compiler-ref/pchibicc/include
CFLAGS=-std=c11 -Wall -Wextra -Wvla -Wstrict-prototypes -Wno-switch -fwrapv -Wno-unused -O2
SRCS=file.c string_pool.c hash_table.c cpp.c token.c lex.c main.c
OBJS=$(SRCS:.c=.o)

ifdef DEBUG
	CFLAGS+=-g -Og
ifeq ($(DEBUG),asan)
	CFLAGS+=-fsanitize=address,undefined
endif
endif

cpp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	mkdir -p build
	mv *.o cpp build

clean:
	rm -rf build

.PHONY: clean
