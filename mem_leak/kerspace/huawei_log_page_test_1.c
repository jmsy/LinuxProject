#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/mm_types.h>
#include<linux/mm.h>
#include<linux/gfp.h>
#include<linux/vmalloc.h>
#include"huawei_log_page.h"
#include"huawei_log_page_test_1.h"

void *addr;
struct page *global_page;

void page_busy_test(struct page **pagep, unsigned int order)
{
    struct page *page;
    page = alloc_pages(GFP_KERNEL, order);
    *pagep = page;
    addr = page_address(page);

    printk(KERN_INFO "------------Hi all, %s\n", __func__);


    printk("page = %p\n", page);
    printk("page_address(addr) = %p\n", addr);
    printk("alloc_pages(GFP_KERNEL, %d) = %p\n", order, addr);
    print_page(page);
}
EXPORT_SYMBOL(page_busy_test);

void start(void)
{
    page_busy_test(&global_page, 4);
}

int huawei_log_page_test_1_init(void)
{
	printk(KERN_INFO "------------Hi all, %s\n", __func__);

//    start();

	return 0;
}

void page_busy_test_exit(struct page **pagep, unsigned int order)
{
    struct page *page;
    page = *pagep;

    __free_page(page);
    printk("__free_pages(%p, %d)\n", page, order);
    print_page(page);
    printk(KERN_INFO "------------Goodbye %s\n", __func__);
}
EXPORT_SYMBOL(page_busy_test_exit);

void end(void)
{
    page_busy_test_exit(&global_page, 4);
}

void huawei_log_page_test_1_exit(void)
{
  //  end();

	printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_log_page_test_1_init);
module_exit(huawei_log_page_test_1_exit);
