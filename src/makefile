OS = $(shell uname)

DEFINES = _POSIX_C_SOURCE=200112L EVIL_RUN_TESTS=1
INCLUDEDIRS = /usr/local/include ../include

CFLAGS = -g -Wall -Wextra -Werror -pedantic
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HEADERS = $(wildcard *.h)

ifeq ($(OS), Darwin)
    CC = clang
else
    CC = gcc
endif

ifndef CHECK89
    CFLAGS := $(CFLAGS) -std=c99
else
    CFLAGS := $(CFLAGS) -std=c89 -Wno-long-long
endif

ifdef OPT
    CFLAGS := $(CFLAGS) -O$(OPT) -DNDEBUG
endif

%.o : %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS) $(addprefix -I, $(INCLUDEDIRS)) $(addprefix -D, $(DEFINES))

libevil.a : $(OBJS)
	if test -f $@; then rm $@; fi
	ar -r $@ $(OBJS)

.PHONY : clean
clean :
	rm *.o
	rm libevil.a

.PHONY : all
all: libevil.a

