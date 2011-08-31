#obj-m += page.o
#obj-m += sizeof.o

#obj-m += huawei_log_page.o
#obj-m += print_log_page_all.o
#obj-m += huawei_log_page_test_1.o
#obj-m += huawei_log_page_test_2.o
#obj-m += huawei_log_page_test_3.o
#obj-m += huawei_log_page_test_4.o

obj-m += huawei_user_space_mem.o
obj-m += huawei_user_space_mem_test.o

#相当于gcc -O0 a.c，关闭编译时的优化
#EXTRA_CFLAGS += -O0

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
