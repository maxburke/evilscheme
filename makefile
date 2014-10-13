OS = $(shell uname)

CFLAGS = -Wall -Wextra -pedantic -g -Werror
DEFINES = _POSIX_C_SOURCE=200112L EVIL_RUN_TESTS=1
INCLUDEDIRS = /usr/local/include src include tests
LIBS = tests/libevil_test.a src/libevil.a -lm
LDFLAGS = -g
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HEADERS = $(wildcard *.h)

ifndef CHECK89
    CFLAGS := $(CFLAGS) -std=c99
else
    CFLAGS := $(CFLAGS) -std=c89 -Wno-long-long
endif

ifeq ($(OS), Darwin)
    CC = clang
    LD = clang
else
    CC = gcc
    LD = gcc
endif

ifdef OPT
    CFLAGS := $(CFLAGS) -O$(OPT) -DNDEBUG
endif

.PHONY: all src tests clean-src clean-tests
all: r4rs

src tests:
	$(MAKE) --directory=$@ $(MAKEFLAGS)

clean-src clean-tests:
	$(MAKE) --directory=$(subst clean-,,$@) clean

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $< $(addprefix -I, $(INCLUDEDIRS)) $(addprefix -D, $(DEFINES))

r4rs : $(OBJS) src tests
	$(LD) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

clean : clean-src clean-tests
	rm *.o
	rm r4rs

