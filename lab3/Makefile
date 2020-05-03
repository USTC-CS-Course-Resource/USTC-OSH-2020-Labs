v = release

ifeq ($(v), debug)
ALL: 1.cpp 2.cpp 3.cpp
	@g++ 1.cpp -o 1 -lpthread -g
	$(info [debug version] 1 created.)
	@g++ 2.cpp -o 2 -lpthread -g
	$(info [debug version] 2 created.)
	@g++ 3.cpp -o 3 -g
	$(info [debug version] 3 created.)
else
ALL: 1.cpp 2.cpp 3.cpp
	@g++ 1.cpp -o 1 -lpthread
	$(info [release version] 1 created.)
	@g++ 2.cpp -o 2 -lpthread
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
