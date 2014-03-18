CFLAGS = -g -O3 -Wall -Wextra -Wno-unused-parameter -Isrc

ifneq ($(OS),Windows_NT)
	CFLAGS += -fPIC
endif

HOEDOWN_SRC=\
	src/autolink.o \
	src/buffer.o \
	src/document.o \
	src/escape.o \
	src/html.o \
	src/html_blocks.o \
	src/html_smartypants.o \
	src/stack.o \
	src/version.o

.PHONY:		all test test-pl clean

all:		libhoedown.so hoedown smartypants

# Libraries

libhoedown.so: libhoedown.so.1
	ln -f -s $^ $@

libhoedown.so.1: $(HOEDOWN_SRC)
	$(CC) $(LDFLAGS) -shared $^ -o $@

libhoedown.a: $(HOEDOWN_SRC)
	$(AR) rcs libhoedown.a $^

# Executables

hoedown: examples/hoedown.o $(HOEDOWN_SRC)
	$(CC) $(LDFLAGS) $^ -o $@

smartypants: examples/smartypants.o $(HOEDOWN_SRC)
	$(CC) $(LDFLAGS) $^ -o $@

# Perfect hashing

src/html_blocks.c: html_block_names.gperf
	gperf -L ANSI-C -N hoedown_find_block_tag -c -C -E -S 1 --ignore-case -m100 $^ > $@

# Testing

test: hoedown
	test/runner.sh ./hoedown test/MarkdownTest_1.0.3/Tests

test-pl: hoedown
	perl test/MarkdownTest_1.0.3/MarkdownTest.pl \
		--script=./hoedown --testdir=test/MarkdownTest_1.0.3/Tests --tidy

test-special-attribute: hoedown
	test/runner.sh "./hoedown --special_attribute --tables --fenced_code" test/MarkdownTest_1.0.3/Tests_Special_Attribute

# Housekeeping

clean:
	$(RM) src/*.o examples/*.o
	$(RM) libhoedown.so libhoedown.so.1 libhoedown.a
	$(RM) hoedown smartypants hoedown.exe smartypants.exe

# Generic object compilations

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
