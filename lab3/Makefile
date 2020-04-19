v = release

ifeq ($(v), debug)
ALL: 1.c 2.c 3.c
	@gcc 1.c -o 1 -lpthread -g
	@gcc 2.c -o 2 -lpthread -g
	@gcc 3.c -o 3 -g
	$(info [debug version])
else
ALL: 1.c 2.c 3.c
	@gcc 1.c -o 1 -lpthread
	@gcc 2.c -o 2 -lpthread
	@gcc 3.c -o 3
	$(info [release version])
endif

.PHONY: run1
run1:
	./1 6666

.PHONY: run2
run2:
	./2 6666

.PHONY: run3
run3:
	./3 6666

.PHONY: gen
gen:
	gcc data_generator.c -o data_generator && ./data_generator
