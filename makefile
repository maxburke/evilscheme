CC = gcc -Wall -Wextra -pedantic -g
r4rs : environment.o object.o read.o builtins.o main.o
clean :
	rm *.o
	rm r4rs
