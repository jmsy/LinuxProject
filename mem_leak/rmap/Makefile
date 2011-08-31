obj-m += rmap.o
obj-m += rmap_test.o
##obj-m += prio_tree.o
all:
	make -C /lib/modules/${shell uname -r}/build/ M=${PWD} modules
clean:
	make -C /lib/modules/${shell uname -r}/build/ M=${PWD} clean
