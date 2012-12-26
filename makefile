CC = gcc -Wall -Wextra -pedantic -g
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o : %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS)

r4rs : $(OBJS)
	gcc -g -o $@ $^ $(CFLAGS)

clean :
	rm *.o
	rm r4rs
