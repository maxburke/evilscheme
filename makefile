CC = gcc
LD = gcc
CFLAGS = -Wall -Wextra -pedantic -g
LDFLAGS = -g
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HEADERS = $(wildcard *.h)

ifndef CHECK89
    CFLAGS := $(CFLAGS) -Werror -std=c99
else
    CFLAGS := $(CFLAGS) -std=c89
endif

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $< $(CFLAGS)

r4rs : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(CFLAGS)

clean :
	rm *.o
	rm r4rs
