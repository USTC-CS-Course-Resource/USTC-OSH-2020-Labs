SHELL: command.o shell.o
	gcc shell.o command.o -o sh
shell.o: shell.c
	gcc -c shell.c
command.o: command.c command.h
	gcc -c command.c

clean:
	rm shell.o
	rm command.o

run:
	./sh