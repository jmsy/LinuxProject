obj-m += memcheck.o
EXTRA_FLAGS += -00
all:
	make -C /lib/modules/${shell uname -r}/build/ M=${PWD} modules
clean:
	make -C /lib/modules/${shell uname -r}/build/ M=${PWD} clean
