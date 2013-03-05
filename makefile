OS = $(shell uname)

ifeq ($(OS), Darwin)
    CC = clang
    LD = clang
else
    CC = gcc
    LD = gcc
endif

CFLAGS = -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L
LDFLAGS = -g
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HEADERS = $(wildcard *.h)

ifndef CHECK89
    CFLAGS := $(CFLAGS) -Werror -std=c99
else
    CFLAGS := $(CFLAGS) -std=c89
endif

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

r4rs : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

clean :
	rm *.o
	rm r4rs
