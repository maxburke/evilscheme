OS = $(shell uname)

CFLAGS = -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L -DEVIL_RUN_TESTS -Werror
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

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

r4rs : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

clean :
	rm *.o
	rm r4rs
