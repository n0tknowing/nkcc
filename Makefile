CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -Wvla -Wstrict-prototypes -Wno-switch -fwrapv

BUILDDIR=build
SRCS=buffer.c file.c string_pool.c hash_table.c cpp.c token.c lex.c main.c
OBJS=$(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

ifdef DEBUG
	CFLAGS+=-g -Og
	CFLAGS+=-DXXH_NO_INLINE_HINTS=1
ifeq ($(DEBUG),asan)
	CFLAGS+=-fsanitize=address,undefined
endif
else
	CFLAGS+=-O2
endif

$(BUILDDIR)/cpp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf build

.PHONY: clean
