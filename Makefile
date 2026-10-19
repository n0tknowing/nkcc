CXX=g++
CXXFLAGS=-std=c++20 -Wall -Wextra -Wvla -Wstrict-aliasing -Wno-switch \
		 -Wno-switch -fwrapv -fno-exceptions -fno-rtti

BUILDDIR=build
SRCS=buffer.cc file.cc string_pool.cc hash_table.cc cpp.cc token.cc lex.cc \
	 main.cc
OBJS=$(addprefix $(BUILDDIR)/,$(SRCS:.cc=.o))

ifdef DEBUG
	CXXFLAGS+=-g -Og
	CXXFLAGS+=-DXXH_NO_INLINE_HINTS=1
ifeq ($(DEBUG),asan)
	CXXFLAGS+=-fsanitize=address,undefined
endif
else
	CXXFLAGS+=-O2 -march=native -flto
endif

$(BUILDDIR)/nkcc: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.cc | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf build

.PHONY: clean
