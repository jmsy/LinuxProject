#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/mm_types.h>
#include<linux/mm.h>
#include<linux/gfp.h>
#include<linux/vmalloc.h>
#include"huawei_log_page.h"
#include"huawei_log_page_test_1.h"
#include"huawei_log_page_test_2.h"

void huawei_log_page_test_2_alloc(struct page **pagep, unsigned int order)
{
    printk(KERN_INFO "------------Hi all, %s\n", __func__);

    page_busy_test(pagep, order);
}
EXPORT_SYMBOL(huawei_log_page_test_2_alloc);

int huawei_log_page_test_2_init(void)
{
	printk(KERN_INFO "------------Hi all, %s\n", __func__);

	return 0;
}

void huawei_log_page_test_2_free(struct page **pagep, unsigned int order)
{
    page_busy_test_exit(pagep, order);
    printk(KERN_INFO "------------Goodbye %s\n", __func__);
}
EXPORT_SYMBOL(huawei_log_page_test_2_free);

void huawei_log_page_test_2_exit(void)
{
	printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_log_page_test_2_init);
module_exit(huawei_log_page_test_2_exit);
