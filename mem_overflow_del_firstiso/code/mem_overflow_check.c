#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <asm/page.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define PROC_NAME	"overinfo"

#define ISOSIZE_KMALLOC 20
#define ISOSIZE_DMA 20
#define ISOSIZE_VMALLOC 20
#define ISOSIZE_GFPS 20
#define ISOSIZE_PAGE 20
#ifndef SLEEP_MILLI_SEC
#define SLEEP_MILLI_SEC(nMilliSec) \
	do{ \
		long timeout = nMilliSec*HZ/1000; \
		set_current_state(TASK_UNINTERRUPTIBLE); \
		while(timeout>0) \
		timeout = schedule_timeout(timeout); \
	}while(0);
#endif
/*自旋锁，用来控制对全局链表的互斥访问。链表存储的是*/
/*各个隔离带的信息，见下面struct iso_zone*/
spinlock_t list_lock = SPIN_LOCK_UNLOCKED;
MODULE_LICENSE("Dual BSD/GPL");
struct list_head iso_head;
LIST_HEAD(iso_head); 
EXPORT_SYMBOL(iso_head);
/*内核线程描述符，用来周期扫描全局链表*/
static struct task_struct *checklist=NULL;
/*存储每个隔离带信息的结构*/
struct iso_zone   
{
	struct list_head list;
	char *callerinfo;  //内存拥有者信息
	int size;   
	void *addr;  //需隔离的内存起始地址
	void *mem_addr;//存储申请内存的首地址
	bool flag;  //扫描用标志位
};

/*对kmalloc封装*/
void *skmalloc(size_t size,gfp_t flags)
{
	int i;
	char *iso_addr;  //前后两个隔离带首地址
	void *ret;  
	char *buffer;
	void *temp_ptr;
	unsigned long ret_addr;
	struct iso_zone *a=NULL;
	temp_ptr = kmalloc(size+ISOSIZE_KMALLOC,flags);
	if(NULL!=temp_ptr)
	{	ret = temp_ptr;
		iso_addr = ret+size;
		/*用‘a’填充隔离带*/
		for(i=0;i<ISOSIZE_KMALLOC;i++)
		{
			*(iso_addr+i) = 'a';
		}
		/*获取此函数的返回地址，以此推出函数调用这信息*/
		ret_addr = (unsigned long)__builtin_return_address(0);
		buffer = (char *)kmalloc(200,GFP_KERNEL);
		/*调用这信息*/
		if(NULL==buffer||(unsigned long)0==ret_addr)
		{
			printk("kmalloc:申请buffer失败!\n");
			return NULL;
		}
		sprint_symbol(buffer,ret_addr);
		/*初始化结构体，其中记录隔离带的信息*/
		a = (struct iso_zone*)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL!=a)
		{
			a[0].callerinfo = buffer;
			a[0].size = ISOSIZE_KMALLOC;
			a[0].addr = iso_addr;
			a[0].mem_addr = ret;
			a[0].flag = false;
			buffer=NULL;
			/*链表互斥插入*/
			spin_lock(&list_lock);
			list_add(&a[0].list,&iso_head);
			spin_unlock(&list_lock);
			return ret;
		}
		else
		{
			printk("申请结构体失败！\n");
			return NULL;
		}
	}
	else{
		printk(KERN_INFO "kmalloc failure!\n");
		return NULL;
	}
}
EXPORT_SYMBOL(skmalloc);

/*kfree的封装函数*/
void skfree(const void *objp)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	/*当初所分配内存的起始地址，包括隔离带（第一隔离带*/
	/*首地址）*/
	if(NULL!=objp)
	{
		spin_lock(&list_lock);	
		list_for_each(pos,&iso_head)
		{
			temp = list_entry(pos,struct iso_zone,list);
			if(objp==temp->mem_addr)
			{
				/*获取第二隔离带结构体指针*/
				/*互斥删除前后隔离带*/
				list_del(pos); 
				break;
			}
		}
	 	spin_unlock(&list_lock);
		//只有一个callerinfo
		kfree(temp->callerinfo); //缓冲信息也释放掉
	//	kfree(temp2->callerinfo);
		kfree(temp);
		/*释放用skmalloc申请的内存，包括俩隔离带*/
		kfree(objp);
	}
}
EXPORT_SYMBOL(skfree);
/*vmalloc的封装函数*/
void *svmalloc(unsigned long size)
{
	int i;
	void *temp_addr;
	void *ret;
	char *iso_addr;
	unsigned long ret_addr;
	char *buffer=NULL;
	struct iso_zone *a=NULL;
	size_t ssize=size+ISOSIZE_VMALLOC;
	temp_addr=vmalloc(ssize);
	if(NULL!=temp_addr)
	{
		ret=temp_addr;
		iso_addr=(char *)temp_addr+size;
		for(i=0;i<ISOSIZE_VMALLOC;i++)
		{
			*(iso_addr+i)='a';
		}
		ret_addr=(unsigned long)__builtin_return_address(0);
		buffer=(char *)kmalloc(200,GFP_KERNEL);
		if(NULL==buffer||(unsigned long)0==ret_addr)
		{	
			printk("vmalloc:申请buffer失败!\n");
			return NULL;
		}
		sprint_symbol(buffer,ret_addr);
		a=(struct iso_zone *)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL!=a)
		{
			a[0].callerinfo=buffer;
			a[0].size=ISOSIZE_VMALLOC;
			a[0].addr=iso_addr;
			a[0].mem_addr=ret;  
			a[0].flag=false;

			buffer=NULL;
			spin_lock(&list_lock);
			list_add(&a[0].list,&iso_head);
			spin_unlock(&list_lock);
			return ret;
		}
		else
		{
			printk("申请结构体失败！\n");
			return NULL;
		}
	}
	else
	{
		printk("vmalloc failure!\n");
		return NULL;
	}
}
EXPORT_SYMBOL(svmalloc);
/*vfree的封装函数*/
void svfree(const void *addr)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	if(addr!=NULL)
	{
		spin_lock(&list_lock);
		list_for_each(pos,&iso_head)
		{
			temp = list_entry(pos,struct iso_zone,list);
			if(addr==temp->mem_addr)
			{
				//spin_lock(&list_lock);
				list_del(pos); 
				//spin_unlock(&list_lock);
			//	kfree(temp);
				break;
			}
		}
		spin_unlock(&list_lock);

		kfree(temp->callerinfo);
		kfree(temp);
		vfree(addr);
	}
}
EXPORT_SYMBOL(svfree);

/*对应dma_alloc_coherent*/
void *sdma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,gfp_t gfp)
{
	int i;
	void *temp_addr;
	void *ret;
	char *iso_addr;
	size_t ssize;
	unsigned long ret_addr;
	char *buffer=NULL;
	struct iso_zone *a=NULL;
	ssize=size+ISOSIZE_DMA;
	temp_addr=dma_alloc_coherent(dev,ssize,dma_handle,gfp);
	if(NULL!=temp_addr)
	{
		ret=temp_addr;
		iso_addr=(char *)temp_addr+size;
		for(i=0;i<ISOSIZE_DMA;i++)
		{
			*(iso_addr+i)='a';
		}
		ret_addr=(unsigned long)__builtin_return_address(0);
		buffer=(char *)kmalloc(200,GFP_KERNEL);
		if(NULL==buffer||(unsigned long)0==ret_addr)
		{
			printk("dma_alloc_coherent:buffer分配失败\n");
			return NULL;
		}
		sprint_symbol(buffer,ret_addr);
		a=(struct iso_zone *)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL==a)
		{
			printk("dma_alloc_coherent:结构体分配失败\n");
			return NULL;
		}
		a[0].callerinfo=buffer;
		a[0].size=ISOSIZE_DMA;
		a[0].addr=iso_addr;
		a[0].mem_addr=ret;
		a[0].flag=false;
		buffer=NULL;
		spin_lock(&list_lock);
		list_add(&a[0].list,&iso_head);
		spin_unlock(&list_lock);
		a=NULL;
		return ret;
	}
	else
	{
		printk("dma_alloc_coherent failure!\n");
		return NULL;
	}
}
EXPORT_SYMBOL(sdma_alloc_coherent);

/*对应dma_free_coherent*/
void sdma_free_coherent(struct device *dev,size_t size,void *vaddr,dma_addr_t bus)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	if(NULL==vaddr)
		return;
	spin_lock(&list_lock);
	list_for_each(pos,&iso_head)
	{
		temp = list_entry(pos,struct iso_zone,list);
		if(vaddr==temp->mem_addr)
		{
			list_del(pos); //delete the first isolation
			break;
		}
	}
	spin_unlock(&list_lock);
	kfree(temp->callerinfo);
	kfree(temp);
	dma_free_coherent(dev,size+ISOSIZE_DMA,vaddr,bus);

}
EXPORT_SYMBOL(sdma_free_coherent);

/*__get_free_pages封装实现1：倍增方法。无需修改参数，安全，但内存浪费较多*/
unsigned long __sget_free_pages(gfp_t gfp_mask, unsigned int order)
{
	int i;
	unsigned long temp_addr;
	unsigned long ret;
	char *iso_addr;
	unsigned long ret_addr;
	char *buffer=NULL;
	struct iso_zone *a=NULL;
	unsigned int sorder=order+1;
	int iso_size=(1<<order)*PAGE_SIZE;
	temp_addr=__get_free_pages(gfp_mask,sorder);
	if(NULL!=(void *)temp_addr)
	{
		ret=temp_addr;
		iso_addr=(char *)temp_addr+(1<<order)*PAGE_SIZE;
		for(i=0;i<iso_size;i++)
		{
			*(iso_addr+i)='a';
		}
		ret_addr=(unsigned long)__builtin_return_address(0);
		buffer=(char *)kmalloc(200,GFP_KERNEL);
		if(NULL==buffer||(unsigned long)0==ret_addr)
		{
			printk("__get_free_pages:申请buffer失败!\n");
			return 0;
		}
		sprint_symbol(buffer,ret_addr);

		a=(struct iso_zone *)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL==a)
		{	
			printk("__get_free_pages:申请结构体失败!\n");
			return 0;
		}
		a[0].callerinfo=buffer;
		a[0].size=(1<<order)*PAGE_SIZE;
		a[0].addr=iso_addr;
		a[0].flag=false;
		a[0].mem_addr=(void *)ret;
		buffer=NULL;
		spin_lock(&list_lock);
		list_add(&a[0].list,&iso_head);
		spin_unlock(&list_lock);
		a=NULL;
		return ret;
	}
	else
	{
		printk("__sget_free_pages failure!\n");
		return 0;
	}
}
EXPORT_SYMBOL(__sget_free_pages);

unsigned long __sget_free_page(gfp_t gfp_mask)
{
	return __sget_free_pages(gfp_mask,0);
}
EXPORT_SYMBOL(__sget_free_page);

unsigned long __sget_dma_pages(gfp_t gfp_mask,unsigned int order)
{
	return __sget_free_pages(gfp_mask | __GFP_DMA,order);
}
EXPORT_SYMBOL(__sget_dma_pages);

/*free_pages的封装，对应__get_free_pages*/
void sfree_pages(unsigned long addr, unsigned int order)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	if((unsigned long)0==addr)
		return;
	spin_lock(&list_lock);
	list_for_each(pos,&iso_head)
	{
		temp = list_entry(pos,struct iso_zone,list);
		if(addr==(unsigned long)temp->mem_addr)
		{
			list_del(pos); //delete the first isolation
			break;
		}
	}
	spin_unlock(&list_lock);
	kfree(temp->callerinfo);
	kfree(temp);
	free_pages(addr,order+1);
}
EXPORT_SYMBOL(sfree_pages);

void sfree_page(unsigned long addr)
{
	sfree_pages(addr,0);
}

/*__get_free_pages版本2：非倍增方式，利用多分配的内存。有时*/
/*会误报溢出*/
unsigned long __nget_free_pages(gfp_t gfp_mask, unsigned int order)
{
	int i;
	unsigned long temp_addr;
	unsigned long ret;
	char *iso_addr;
	unsigned long ret_addr;
	char *buffer=NULL;
	struct iso_zone *a=NULL;
	temp_addr=__get_free_pages(gfp_mask,order);
	if(NULL!=(void *)temp_addr)
	{
		ret=temp_addr;
		iso_addr=(char *)temp_addr+(1<<order)*PAGE_SIZE-ISOSIZE_GFPS;
		for(i=0;i<ISOSIZE_GFPS;i++)
		{
			*(iso_addr+i)='a';
		}
		ret_addr=(unsigned long)__builtin_return_address(0);
		buffer=(char *)kmalloc(200,GFP_KERNEL);
		if(buffer==NULL||(unsigned long)0==ret_addr)
		{
			printk("__get_free_pages:申请buffer失败!\n");
			return 0;
		}
		sprint_symbol(buffer,ret_addr);
		a=(struct iso_zone *)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL==a)
		{
			printk("__get_free_pages:申请结构体失败!\n");
			return 0;
		}
		a[0].callerinfo=buffer;
		a[0].size=ISOSIZE_GFPS;
		a[0].addr=iso_addr;
		a[0].mem_addr=(void *)ret;
		a[0].flag=false;
		buffer=NULL;
		spin_lock(&list_lock);
		list_add(&a[0].list,&iso_head);
		spin_unlock(&list_lock);
		a=NULL;
		return ret;
	}
	else
	{
		printk("__nget_free_pages failure!\n");
		return 0;
	}
}
EXPORT_SYMBOL(__nget_free_pages);
void nfree_pages(unsigned long addr, unsigned int order)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	if((unsigned long)0==addr)
		return;
	spin_lock(&list_lock);
	list_for_each(pos,&iso_head)
	{
		temp = list_entry(pos,struct iso_zone,list);
		if(addr==(unsigned long)temp->mem_addr)
		{
			list_del(pos); //delete the first isolation
			break;
		}
	}
	spin_unlock(&list_lock);
	kfree(temp->callerinfo);
	kfree(temp);
	free_pages(addr,order);

}
EXPORT_SYMBOL(nfree_pages);

/*如果参数给的是size而非order，则可以先分配一部分，再释放，*/
/*这样既保证了安全性有保证不会有超过一个页面的页面被浪费*/
void *__get_free_pages_ex(gfp_t gfp_mask,size_t size)
{
	int i;
	void * temp_addr;
	void * ret;
	char *iso_addr;
	unsigned long ret_addr;
	char *buffer=NULL;
	struct iso_zone *a=NULL;
	temp_addr=alloc_pages_exact(size+ISOSIZE_PAGE,gfp_mask);
	if(NULL!=temp_addr)
	{
		ret=temp_addr;
		iso_addr=(char *)temp_addr+size;
		for(i=0;i<ISOSIZE_PAGE;i++)
		{
			*(iso_addr+i)='a';
		}
		ret_addr=(unsigned long)__builtin_return_address(0);
		buffer=(char *)kmalloc(200,GFP_KERNEL);
		if(NULL==buffer||(unsigned long)0==ret_addr)
		{
			printk("__get_free_pages_ex:申请buffer失败!\n");
			return NULL;
		}	
		sprint_symbol(buffer,ret_addr);
		a=(struct iso_zone *)kmalloc(sizeof(struct iso_zone),GFP_KERNEL);
		if(NULL==a)
		{
			printk("__get_free_pages_ex:申请结构体失败!\n");
			return NULL;
		}
		a[0].callerinfo=buffer;
		a[0].size=ISOSIZE_PAGE;
		a[0].addr=iso_addr;
		a[0].mem_addr=ret;
		a[0].flag=false;
		buffer=NULL;
		spin_lock(&list_lock);
		list_add(&a[0].list,&iso_head);
		spin_unlock(&list_lock);
		a=NULL;
		return ret;
	}
	else
	{
		printk("get_free_pages failure!\n");
		return 0;
	}
}
EXPORT_SYMBOL(__get_free_pages_ex);
void free_pages_ex(void * addr, size_t size)
{
	struct list_head *pos;
	struct iso_zone *temp=NULL;
	if(NULL==addr)
		return ;
	spin_lock(&list_lock);
	list_for_each(pos,&iso_head)
	{
		temp = list_entry(pos,struct iso_zone,list);
		if(addr==(void *)temp->mem_addr)
		{
			list_del(pos); //delete the first isolation
			break;
		}
	}
	spin_unlock(&list_lock);
	kfree(temp->callerinfo);
	kfree(temp);
	free_pages_exact(addr,size+ISOSIZE_PAGE);

}
EXPORT_SYMBOL(free_pages_ex);
/*check the isolation to find if the memory has overflow!*/
void check_iso(void)
{
	struct list_head *pos;
	struct iso_zone *ops;
	char *checkptr;
	int iso_size;
	char *buffer=NULL;
	spin_lock(&list_lock);
	if(!list_empty(&iso_head))
	{
		list_for_each(pos,&iso_head)
		{
			ops = list_entry(pos,struct iso_zone,list);
			if(!ops->flag)
			{
				checkptr= (char *)ops->addr;
				iso_size = ops->size;
				while(iso_size--)
				{
					if(*checkptr!='a')
					{
						ops->flag = true;//next time the check function will not check this point
						printk("[memory overflow]: ");
						printk("more infomation: %s\n",ops->callerinfo);
						break;
					}
					checkptr++;
				}//endwhie
			}
		}
	}//endif
	
	spin_unlock(&list_lock);
	if(buffer!=NULL)
	{
		printk("memort overflow:\n");
		printk("%s ",buffer);
		buffer=NULL;
	}
}

/*内核线程要执行的函数*/
int checklist_func(void *para)
{
	while(!kthread_should_stop())
	{
		SLEEP_MILLI_SEC(1000);//wait for 2sec
		check_iso();
	}
	return 0;
}

static void *my_seq_start(struct seq_file *s,loff_t *pos)
{
	static unsigned long counter = 0;
	if(*pos==0)
		return &counter;
	else
	{
		pos=0;
		return NULL;
	}

}
static void *my_seq_next(struct seq_file* s,void *v,loff_t *pos)
{
	//unsigned long *tmp_v=(unsigned long *)v;
	//(*tmp_v)++;
	(*pos)++;
	return NULL;
}

static void my_seq_stop(struct seq_file *s,void *v)
{
}

static int my_seq_show(struct seq_file *s,void *v)
{
//	loff_t *spos=(loff_t *)v;
//	seq_printf(s,"%Ld\n",*spos);

	struct list_head *pos;
	struct iso_zone *ops;
	char *checkptr;
	int iso_size;
	spin_lock(&list_lock);
	if(!list_empty(&iso_head))
	{
		list_for_each(pos,&iso_head)
		{
			ops = list_entry(pos,struct iso_zone,list);
			if(!ops->flag)
			{
				checkptr= (char *)ops->addr;
				iso_size = ops->size;
				while(iso_size--)
				{
					if(*checkptr!='a')
					{
					//	ops->flag = true;//next time the check function will not check this point
						//printk("[memory overflow]: ");
						seq_printf(s,"[memory overflow]: ");
						//printk("more infomation: %s\n",ops->callerinfo);
						seq_printf(s,"more infomation: %s\n",ops->callerinfo);
						break;
					}
					checkptr++;
				}//endwhie
				//printk("\n");
				seq_printf(s,"\n");
			}
		}
	}//endif
	//else
	//  printk("the list is empty!\n");
	
	spin_unlock(&list_lock);

	return 0;
}

static struct seq_operations my_seq_ops = {
	.start=my_seq_start,
	.next=my_seq_next,
	.stop=my_seq_stop,
	.show=my_seq_show
};

static int my_open(struct inode *inode,struct file *file)
{
	return seq_open(file,&my_seq_ops);
}

static struct file_operations my_file_ops={
	.owner=THIS_MODULE,
	.open=my_open,
	.read=seq_read,
	.llseek=seq_lseek,
	.release=seq_release
};

static int init_proc(void)
{
	struct proc_dir_entry *entry;
	entry = create_proc_entry(PROC_NAME,0,NULL);
	if(entry)
	  entry->proc_fops = &my_file_ops;
	return 0;
}

static void exit_proc(void)
{
	remove_proc_entry(PROC_NAME,NULL);
}
static int kmalloc_init(void)
{
	int err;
	checklist = kthread_run(checklist_func,NULL,"checklist");
	if(IS_ERR(checklist))
	{
		printk("unable to start kernel thread!\n");
		err=PTR_ERR(checklist);
		checklist=NULL;
		return err;
	}
	return 0;
/*	struct proc_dir_entry *entry;
	entry = create_proc_entry(PROC_NAME,0,NULL);
	if(entry)
	  entry->proc_fops = &my_file_ops;
	return 0;
*/
}
static void kmalloc_exit(void)
{
	//kill the kernel thread
	if(checklist)
	{
		kthread_stop(checklist);
		checklist=NULL;
	}
//	remove_proc_entry(PROC_NAME,NULL);

}
module_init(kmalloc_init);
module_exit(kmalloc_exit);
