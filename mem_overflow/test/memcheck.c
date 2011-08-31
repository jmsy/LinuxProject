#include <linux/init.h>
#include <linux/module.h>
#include "mem_overflow_check.h"
#define size 3000
int *ptr1=NULL;
int *ptr2=NULL;
char *ptr3=NULL;
char *ptr4=NULL;
char *ptr5=NULL;
char *ptr6=NULL;
char *ptr7=NULL;
static int check_init(void)
{
	int i=0;
	ptr1=(int *)skmalloc(10*sizeof(int),GFP_KERNEL); 
	printk("skmalloc: \n");
	if(NULL!=ptr1)
	{
		for(i=0;i<=10;i++)
		{
			ptr1[i]=i;//写越界
			//printk("%d ",ptr1[i]);
		}
	}
	ptr2=(int *)svmalloc(10*sizeof(int));
	printk("svmalloc: \n");
	if(NULL!=ptr2)
	{
		for(i=0;i<=10;i++)
		{
			ptr2[i]=i;//写越界
			//printk("%d ",ptr2[i]);
		}
	}
	ptr3=(char *)__get_free_pages_ex(GFP_KERNEL,size);
	printk("__get_free_pages_exact: \n");
	if(NULL!=ptr3)
	{
		for(i=0;i<size+3;i++)
		{
			ptr3[i]='b';
		}
	}
	

	ptr4=(char *)__sget_free_pages(GFP_KERNEL,2);
	if(NULL!=ptr4)
	{
		for(i=0;i<(1<<2)*PAGE_SIZE+10;i++)
		  ptr4[i]='b';
	}

	ptr5=(char *)__nget_free_pages(GFP_KERNEL,2);
	if(NULL!=ptr5)
	{
		for(i=0;i<((1<<2)*PAGE_SIZE-20);i++)
		  ptr5[i]='b';
	}
	ptr6=(char *)__sget_free_page(GFP_KERNEL);
	if(NULL!=ptr6)
	{
		for(i=0;i<PAGE_SIZE+5;i++)
		  ptr6[i]='b';
	}
	ptr7=(char *)__sget_dma_pages(GFP_KERNEL,2);
	if(NULL!=ptr7)
	{
		for(i=0;i<(1<<2)*PAGE_SIZE+5;i++)
		  ptr7[i]='b';
	}
	return 0;
}
static void check_exit(void)
{
	if(NULL!=ptr1)
	  skfree(ptr1);
	if(NULL!=ptr2)
	  svfree(ptr2);
	if(NULL!=ptr3)
	  free_pages_ex(ptr3,size);
	if(NULL!=ptr4)
	  sfree_pages((unsigned long)ptr4,2);
	if(NULL!=ptr5)
	  nfree_pages((unsigned long)ptr5,2);
	if(NULL!=ptr6)
	  sfree_page((unsigned long)ptr6);
	if(NULL!=ptr7)
	  sfree_pages((unsigned long)ptr7,2);
	printk("check module exit!");
}
module_init(check_init);
module_exit(check_exit);
MODULE_LICENSE("GPL");

