CC = gcc -Wall -Wextra -pedantic -g
OBJS = environment.o object.o read.o builtins.o main.o

%.o : %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS)

r4rs : $(OBJS)
	gcc -o $@ $^ $(CFLAGS)

clean :
	rm *.o
	rm r4rs
