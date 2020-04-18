all: 1.c
	gcc 1.c -o 1 -lpthread -g
	gcc 2.c -o 2 -lpthread -g

run1:
	./1 6666

run2:
	./2 6666

gen:
	gcc data_generator.c -o data_generator && ./data_generator