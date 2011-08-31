#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/mm_types.h>
#include<linux/mm.h>
#include<linux/gfp.h>
#include<linux/vmalloc.h>
#include"huawei_log_page_test_2.h"
#include"huawei_log_page_test_3.h"

void huawei_log_page_test_3_alloc(struct page **pagep, unsigned int order)
{
    printk(KERN_INFO "------------Hi all, %s\n", __func__);

    huawei_log_page_test_2_alloc(pagep, order);
}
EXPORT_SYMBOL(huawei_log_page_test_3_alloc);

int huawei_log_page_test_3_init(void)
{
	printk(KERN_INFO "------------Hi all, %s\n", __func__);

	return 0;
}

void huawei_log_page_test_3_free(struct page **pagep, unsigned int order)
{
    huawei_log_page_test_2_free(pagep, order);
    printk(KERN_INFO "------------Goodbye %s\n", __func__);
}
EXPORT_SYMBOL(huawei_log_page_test_3_free);

void huawei_log_page_test_3_exit(void)
{
	printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_log_page_test_3_init);
module_exit(huawei_log_page_test_3_exit);
