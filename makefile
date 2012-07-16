CC = gcc -Wall -Wextra -pedantic -g
OBJS = environment.o object.o read.o builtins.o main.o runtime.o gc.o lambda.o
HEADERS = base.h builtins.h environment.h object.h read.h

%.o : %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS)

r4rs : $(OBJS)
	gcc -o $@ $^ $(CFLAGS)

clean :
	rm *.o
	rm r4rs
