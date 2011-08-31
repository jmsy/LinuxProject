内存泄漏解决方案代码说明

本方案分两部分：
1.劫持内核核心内存分配释放函数以获取内存所有者信息
2.遍历用户态页表查询给定物理内存地址在用户态程序中的分布

以下列出的所有代码文件均在代码文件中有详细注释

========================================================
其中第1部分的实现代码见文件：
huawei_log_page.c 和 huawei_log_page.h。

huawei_log_page.c：方案核心实现代码
huawei_log_page.h：供EXPORT_SYMBOL后其他.c文件include

第1部分的测试代码见文件：
huawei_log_page_test_1.c
huawei_log_page_test_1.h
huawei_log_page_test_2.c
huawei_log_page_test_2.h
huawei_log_page_test_3.c
huawei_log_page_test_3.h
huawei_log_page_test_4.c
huawei_log_page_test_4.h
实现的是test_4模块调用test_3模块，然后test_3调用test_2，test_2调用test_1，test_1调用alloc_page函数，然后查询分配得到的物理内存，看所得结果是否与已知结果一致，包括起始终止物理页框地址，调用栈及模块信息。
========================================================


========================================================
第2部分的实现代码见文件:
huawei_user_space_mem.c和huawei_user_space_mem.h

huawei_user_space_mem.c：方案核心实现代码
huawei_user_space_mem.h：供EXPORT_SYMBOL后其他.c文件include


相关测试代码文件：
huawei_user_space_mem_test.c
测试原理是通过 /proc/[pid]/maps 提供的信息得到已知进程线性内存分布，然后将通过huawei_user_space_mem.c模块提供的信息相比对，看是否一致。
========================================================

其它关键文件：
Makefile  :前期代码编译和测试所使用的Makefile文件
注意：1、分成用户太代码喝内核太代码的原因：劫持的方法只能记录新分配的一块内存的使用者，而在用户态程序中，存在多个进程共享同一块内存的情况，使用劫持不会有共享信息。
2、paadr==0的情况对应该物理内存已经被换倒磁盘上了，所以查不到他的使用信息。
