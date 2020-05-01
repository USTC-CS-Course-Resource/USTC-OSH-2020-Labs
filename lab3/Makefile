v = release

ifeq ($(v), debug)
ALL: 1.c 2.c 3.cpp
	@gcc 1.c -o 1 -lpthread -g
	$(info [debug version] 1 created.)
	@gcc 2.c -o 2 -lpthread -g
	$(info [debug version] 2 created.)
	@g++ 3.cpp -o 3 -g
	$(info [debug version] 3 created.)
else
ALL: 1.c 2.c 3.cpp
	@gcc 1.c -o 1 -lpthread
	$(info [release version] 1 created.)
	@gcc 2.c -o 2 -lpthread
	$(info [release version] 2 created.)
	@g++ 3.cpp -o 3
	$(info [release version] 3 created.)
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
