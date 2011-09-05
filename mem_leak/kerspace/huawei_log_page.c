#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/mm_types.h>
#include<linux/kprobes.h>
#include<linux/vmalloc.h>
#include<linux/rbtree.h>
#include<asm/stacktrace.h>
#include<linux/mm.h>
#include"huawei_log_page.h"
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#define PROC_NAME "leak_info"
#define LOG_PAGE_TRACE_LEVEL	15

#define KPROBES_SKIP_NUM    10

/* 
 * Probe up to MAXACTIVE instances concurrently. 
 * MAXACTIVE <= 0 是设置为默认值，max(10, 2*NR_CPUS0
 */
#define MAXACTIVE -1
static int leak_time=10;
struct list_head node_head;
LIST_HEAD(node_head);
struct log_page_struct {
    union {
        /*
         * 空闲的log_page_struct都在一个单向链表中
         */
        struct log_page_struct *next;

        /*
         * 对于使用中的lp，该处存放__alloc_pages_nodemask时的
         * order参数。
         */
        unsigned int order;
    };
	long alloc_time;
    /*
     * 红黑树所需要的节点
     */
    struct rb_node lp_node;

    /*
     * 该次内存分配事件的记录起始页框和终止页框。
     * 前闭后开: [start_page, end_page)
     */
    struct page *start_page;
    struct page *end_page;

    /*
     * 从0开始。如果那个entry的值是ULONG_MAX，则trace结束。
     * LOG_PAGE_TRACE_LEVEL是追溯的调用栈的深度。
     */
    unsigned long entries[LOG_PAGE_TRACE_LEVEL];
};

static struct lp_pool_struct {
    unsigned long nr_lp;
    struct log_page_struct *pool;
    struct log_page_struct *free;
} lp_pool;

static DEFINE_SPINLOCK(lp_pool_lock);

void free_lp(struct log_page_struct *lp);

int init_lp_pool(void)
{
    unsigned long nr_pages;
    unsigned long size;
    void *addr;
    unsigned long i;

    //nr_pages = get_nr_pages();
	nr_pages = totalram_pages;
    size = nr_pages * sizeof(struct log_page_struct);
    addr = vmalloc(size);
    
    if(!addr)
    {
        printk(KERN_INFO "vmalloc lp_pool(size = %lu) failed.\n",
                size);
        return -1;
    }

    lp_pool.nr_lp = nr_pages;
    lp_pool.pool = (struct log_page_struct *)addr;
    lp_pool.free = NULL;

    for(i=0;i<nr_pages;i++)
    {
        struct log_page_struct *lp;
        lp = &(lp_pool.pool[i]);

        free_lp(lp);
    }

    return 0;
}

void exit_lp_pool(void)
{
    vfree((void*)lp_pool.pool);
}

/*
 * 不可能出现NULL的返回值。
 * 因为即使每一个页框都使用一个log_page_struct，
 * 我们仍然有等同于页框数目的log_page_struct。
 * 数量绝对够。
 */
struct log_page_struct *__alloc_lp(void)
{
    struct log_page_struct *lp;

    spin_lock(&lp_pool_lock);
    lp = lp_pool.free;
    lp_pool.free = lp->next;
    memset(lp, 0, sizeof(struct log_page_struct));
    spin_unlock(&lp_pool_lock);

    return lp;
}   

struct log_page_struct *alloc_lp(void)
{
    return __alloc_lp();
}

void __free_lp(struct log_page_struct *lp)
{
    spin_lock(&lp_pool_lock);
    lp->next = lp_pool.free;
    lp_pool.free = lp;
    spin_unlock(&lp_pool_lock);
}

void free_lp(struct log_page_struct *lp)
{
    __free_lp(lp);
}

static struct rb_root lp_root = RB_ROOT;

/*
 * 加锁的作用
 * 1.防止在红黑树进行删除操作到同时进行添加或删除操作。
 * 2.
 */
static DEFINE_SPINLOCK(lp_lock);

/*
 * 我们已经获得了rb_tree的锁。
 *
 * see 
 * void ext3_rsv_window_add(...); in fs/ext3/balloc.c
 * also
 * Documentation/rbtree.txt
 */
void log_page_insert(struct log_page_struct *lp)
{
    struct rb_root *root = &lp_root;
    struct rb_node *node = &lp->lp_node;
    struct page *end = lp->end_page;

    struct rb_node **p = &root->rb_node;
    struct rb_node *parent = NULL;
    struct log_page_struct *this;

    while( *p )
    {
        parent = *p;
        this = rb_entry(parent, struct log_page_struct, lp_node);

        /*
         * rb_tree example:
         *
         *          order=1
         *          [5,6)
         *         /     \
         *      [4,5)    [6,7)
         *   order=1    order=1
         */
        /*
         * 将要插入rb_tree的内存块肯定不在rb_tree中，
         * 因为系统的buddy和我们的锁可以保证分配出去的
         * 内存肯定不会再次分配。
         * 所以不存在相等的情况。
         */
        if( end <= (this->start_page) )
            p = &((*p)->rb_left);
        else
            p = &((*p)->rb_right);
    }
    
    rb_link_node(node, parent, p);
    rb_insert_color(node, root);
}

/*
 * 类似于 include/linux/stacktrace.h 中的
 * struct stack_trace。增加了next来实现单链表。
 */
struct st {
    unsigned int nr_entries, max_entries;
    unsigned long *entries;
    int skip;
    struct st *next;
};

static struct st_pool_struct {
    unsigned long nr_st;
    struct st *pool;
    struct st *free;
} st_pool;

static DEFINE_SPINLOCK(st_pool_lock);

void free_st(struct st *st);

int init_st_pool(void)
{
    int maxactive = (int)MAXACTIVE;
    void *addr;
    int i;

    if(maxactive <= 0)
    {
#ifdef  CONFIG_PREEMPT
        maxactive = max(10, 2 * NR_CPUS);
#else
        maxactive = NR_CPUS;
#endif
    }

    addr = vmalloc(sizeof(struct st) * maxactive);
    if(!addr)
    {
        printk(KERN_INFO "vmalloc st_pool failed.\n");
        return -1;
    }

    st_pool.nr_st = maxactive;
    st_pool.pool = (struct st*)addr;
    st_pool.free = NULL;

    for(i=0;i<st_pool.nr_st;i++)
    {
        struct st *st;
        st = &(st_pool.pool[i]);

        free_st(st);
    }

    return 0;
}

void exit_st_pool(void)
{
    vfree((void*)st_pool.pool);
}

/*
 * 不可能返回NULL。因为kprobe至多追踪到MAXACTIVE个实例，
 * 所以最多MAXACTIVE个struct stack_trace会被申请，
 * 而初始化时pool中已经有这么多个了。
 */
static inline struct st *__alloc_st(void)
{
    struct st *st;

    spin_lock(&st_pool_lock);
    st = st_pool.free;
    st_pool.free = st->next;
    spin_unlock(&st_pool_lock);

    return st;
}

struct st *alloc_st(void)
{
    return __alloc_st();
}

static inline void __free_st(struct st *st)
{
    spin_lock(&st_pool_lock);
    st->next = st_pool.free;
    st_pool.free = st;
    spin_unlock(&st_pool_lock);
}

void free_st(struct st *st)
{
    __free_st(st);
}

static void hw_save_stack_warning(void *data, char *msg)
{
}

static void
hw_save_stack_warning_symbol(void *data, char *msg, unsigned long symbol)
{
}

static int hw_save_stack_stack(void *data, char *name)
{
	return 0;
}

static void hw_save_stack_address(void *data, unsigned long addr, int reliable)
{
	struct st *trace = data;

	if (trace->skip > 0) {
		trace->skip--;
		return;
	}
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

/*
 * 此处定义及相应函数定义，参考 arch/x86/kernel/stacktrace.c
 */
static const struct stacktrace_ops hw_save_stack_ops = {
	.warning = hw_save_stack_warning,
	.warning_symbol = hw_save_stack_warning_symbol,
	.stack = hw_save_stack_stack,
	.address = hw_save_stack_address,
};

void get_lp_call_stack(struct log_page_struct *lp)
{
    struct st *st;
    unsigned long stack;
    st = alloc_st();

    st->nr_entries = 0;
    st->max_entries = LOG_PAGE_TRACE_LEVEL;
    st->entries = lp->entries;
    st->skip = KPROBES_SKIP_NUM;

    dump_trace(NULL, NULL, &stack, 0, &hw_save_stack_ops, st);

    free_st(st);
}

struct log_page_struct *find_lp(struct page *start)
{
    struct rb_node *node = lp_root.rb_node;
    struct log_page_struct *lp;

    while(node)
    {
        lp = rb_entry(node, struct log_page_struct, lp_node);
        
        if( start < lp->start_page )
            node = node->rb_left;
        else if( start >= lp->end_page)
            node = node->rb_right;
        else
            return lp;
    }
    return NULL;
}

/*
 * 传过来的内存块一定是连续的
 *
 * 特例1.
 * 该内存块没有被记录。
 *
 * 特例2.
 * 若分配了内存块[0,4)共4个页框，
 * 释放时可能只释放 0 一个页框。
 *
 * 特例3.
 * 一次分配了[0,1)，一次分配了[1,2)，
 * 但是可以一次就释放[0,2)
 *
 * 所以不能简单的rb_erase
 */
void do_log_page_free(struct page *page, unsigned int order)
{
    struct page *start = page;
    struct page *end = page + (1<<order);
    struct log_page_struct *lp;

    spin_lock(&lp_lock);
    /*
     * 1.start不在log里面,例如start<lp->start_page, or start>=lp->end_page
     * 2.start == lp->start_page
     *   2.1 end <  lp->end_page：缩小lp
     *   2.2 end == lp->end_page：直接erase
     *   2.3 end >  lp->end_page：start = lp->end_page，循环
     * 3.start > lp->start_page && start < lp->end_page
     *   3.1 end <  lp->end_page：拆分lp
     *   3.2 end == lp->end_page：缩小lp
     *   3.3 end >  lp->end_page：start = lp=>end_page，循环
     *
     * 其他情况我们也不需要考虑，因为buddy和我们的锁保证了不会出现其他情况。
     */
    for(;;)
    {
        lp = find_lp(start);

        /*
         * 1.
         * 可能有的页框在我们的模块启动之前就分配出去了，
         * 现在只是释放它们，所以不会有记录
         */
        if(lp == NULL)
        {
            start++;
            goto check_break;
        }
        /*
         * 2.start == lp->start_page
         */
        else if(start == (lp->start_page))
        {
            /*
             *   2.1 end <  lp->end_page：缩小lp
             */
            if(end < lp->end_page)
            {
                lp->start_page = end;
                start = end;
                goto check_break;
            }
            /*
             *   2.2 end == lp->end_page：直接erase
             */
            else if(end == lp->end_page)
            {
                rb_erase(&lp->lp_node, &lp_root);
                free_lp(lp);
                start = end;
                goto check_break;
            }
            /*
             *   2.3 end >  lp->end_page：start = lp->end_page，循环
             */
            else
            {
                rb_erase(&lp->lp_node, &lp_root);
                free_lp(lp);
                start = lp->end_page;
                goto check_break;
            }
        }
        /*
         * 3.start > lp->start_page && start < lp->end_page
         */
        else
        {
            /*
             *   3.1 end <  lp->end_page：拆分lp
             */
            if(end < lp->end_page)
            {
                struct log_page_struct *new_lp;
                new_lp = alloc_lp();
				if(new_lp)
				{
					memcpy(new_lp, lp, sizeof(struct log_page_struct));

					new_lp->start_page = end;
					new_lp->end_page = lp->end_page;

					lp->end_page = start;

					log_page_insert(new_lp);
				}
            }
            /*
             *   3.2 end == lp->end_page：缩小lp
             */
            else if(end == lp->end_page)
            {
                lp->end_page = start;
                start = end;
                goto check_break;
            }
            /*
             *   3.3 end >  lp->end_page：start = lp=>end_page，循环
             */
            else
            {
                struct page *tmp;
                tmp = start;
                start = lp->end_page;
                lp->end_page = tmp;
                goto check_break;
            }
        }

check_break:
        if( start == end )
            break;
    }
    spin_unlock(&lp_lock);
}

void log_page_free(struct page *page, unsigned int order)
{
    do_log_page_free(page, order);

}

static void print_log_page(struct log_page_struct *lp)
{
    int i;

    if(!lp)
    {
        printk("----- %s ---\n", __func__);
        printk("memory not tracked(free or alloc before we track)\n");
        return;
    }

    printk("start addr = 0x%16lx\n", (unsigned long)page_address(lp->start_page) - PAGE_OFFSET);
    printk(" end  addr = 0x%16lx\n", (unsigned long)page_address(lp->end_page) - PAGE_OFFSET);
    printk("call trace:\n");

    for(i=0;i<LOG_PAGE_TRACE_LEVEL;i++)
    {
        printk("    %2d    :[<0x%16lx>] %pS\n", i, lp->entries[i], (void*)lp->entries[i]);
    }
	printk("alloc_time = %li\n",lp->alloc_time);
}
struct node
{
	struct log_page_struct lp;
	struct list_head list;
};
static void insert_to_list(struct log_page_struct *lp)
{
    int i;
	struct node *new_node;
    if(!lp)
    {
        printk("----- %s ---\n", __func__);
        printk("memory not tracked(free or alloc before we track)\n");
        return;
    }
	new_node = (struct node *)kmalloc(sizeof(struct node),GFP_KERNEL);
	if(new_node)
	{
		memcpy(&(new_node->lp),lp,sizeof(struct log_page_struct));
		list_add(&(new_node->list),&node_head);
	}
	else
	{
		printk("start addr = 0x%16lx\n", (unsigned long)page_address(lp->start_page) - PAGE_OFFSET);
		printk(" end  addr = 0x%16lx\n", (unsigned long)page_address(lp->end_page) - PAGE_OFFSET);
		printk("call trace:\n");

		for(i=0;i<LOG_PAGE_TRACE_LEVEL;i++)
		{
			printk("    %2d    :[<0x%16lx>] %pS\n", i, lp->entries[i], (void*)lp->entries[i]);
		}
	}
}

void unregister_probes(void);
int register_probes(void);
void exchange_tree_to_list(int time)
{
    struct rb_root *root = &lp_root;
    struct log_page_struct *lp;
    struct rb_node *node;
	struct timeval tv;
	do_gettimeofday(&tv);
	unregister_probes();
	unregister_probes();
    spin_lock(&lp_lock);
    lp = rb_entry(rb_first(root), struct log_page_struct, lp_node);

    while(lp)
    {
		if(tv.tv_sec-lp->alloc_time>=time)
		  insert_to_list(lp);
        node = rb_next(&(lp->lp_node));
        if(!node)
            break;
        lp = rb_entry(node, struct log_page_struct, lp_node);
    }
    spin_unlock(&lp_lock);
	register_probes();
}

static void *proc_seq_start(struct seq_file *s,loff_t *pos)
{
	static bool flags = true;
	if(flags)
	{
	  exchange_tree_to_list(leak_time);
	  flags = false;
	}
	if(!list_empty(&node_head))
	  return node_head.next;
	else
	{
		flags = true;
		return NULL;
	}
}

static void *proc_seq_next(struct seq_file *s,void *v,loff_t *pos)
{
	struct list_head *new_node;
	struct node *temp;
	new_node = (struct list_head *)v;
	list_del(new_node);
	temp = list_entry(new_node,struct node,list);
	if(temp)
	  kfree(temp);
	return NULL;

}

static void proc_seq_stop(struct seq_file *s,void *v)
{
}

static int proc_seq_show(struct seq_file *s,void *v)
{
	struct list_head *new_node;
	struct node *temp;
	int i;
	new_node = (struct list_head *)v;
	temp = list_entry(new_node,struct node,list);
	if(temp)
	{
		seq_printf(s,"start addr = 0x%16lx\n",(unsigned long)page_address(temp->lp.start_page)-PAGE_OFFSET);
		seq_printf(s,"end addr = 0x%16lx\n",(unsigned long)page_address(temp->lp.end_page)-PAGE_OFFSET);

		for(i=0;i<LOG_PAGE_TRACE_LEVEL;i++)
		{
			seq_printf(s,"    %2d    :[<0x%16lx>] %pS\n", i, temp->lp.entries[i], (void*)(temp->lp.entries[i]));
		}

	}
	return 0;
}

static struct seq_operations proc_seq_ops = {
	.start=proc_seq_start,
	.next=proc_seq_next,
	.stop=proc_seq_stop,
	.show=proc_seq_show
};

static int proc_open(struct inode *inode,struct file *file)
{
	return seq_open(file,&proc_seq_ops);
}
static struct file_operations proc_file_ops = {
	.owner=THIS_MODULE,
	.open=proc_open,
	.read=seq_read,
	.llseek=seq_lseek,
	.release=seq_release
};

void print_page(struct page *page)
{
    struct log_page_struct *lp;

    spin_lock(&lp_lock);
    lp = find_lp(page);
    print_log_page(lp);
    spin_unlock(&lp_lock);

}
EXPORT_SYMBOL(print_page);

/* per-instance private data */
struct alloc_pages_nodemask_data {
    struct log_page_struct *lp;
};

/*
 * call __alloc_pages_nodemask的时候,order会在%esi中
 */
static int 
alloc_pages_nodemask_entry_handler(struct kretprobe_instance *ri, 
        struct pt_regs *regs)
{
    struct alloc_pages_nodemask_data *data;
    struct log_page_struct *lp;
	struct timeval tv;
	do_gettimeofday(&tv);
    lp = alloc_lp();
	if(lp)
	{
	  lp->order = regs->si;
  	  lp->alloc_time = tv.tv_sec;
  	  /*
  	   * 放到这里来做是因为如果放到ret_handle的话，
  	   * 有时候不能获取到调用栈。
  	   */
  	  get_lp_call_stack(lp);

  	  data = (struct alloc_pages_nodemask_data *)ri->data;
  	  data->lp = lp;
	}

    return 0;
}

static inline void log_page_alloc_with_lp(struct page *page,
        unsigned int order, struct log_page_struct *lp)
{
    lp->start_page = page;
    lp->end_page = page + (1 << order);

    spin_lock(&lp_lock);
    log_page_insert(lp);
    spin_unlock(&lp_lock);

    /*
    printk("%s\n", __func__);
    print_log_page(lp);
    */
}

static int 
alloc_pages_nodemask_ret_handler(struct kretprobe_instance *ri,
        struct pt_regs *regs)
{
    unsigned long retval = regs_return_value(regs);
    struct alloc_pages_nodemask_data *data = (struct alloc_pages_nodemask_data *)ri->data;
    unsigned int order;
    struct page *page;
    struct log_page_struct *lp;

    page = (struct page*)retval;
    lp = (struct log_page_struct*)(data->lp);

    if(!page)
    {
        free_lp(lp);
        return 0;
    }

    order = lp->order;

    log_page_alloc_with_lp(page, order, lp);

    return 0;
}

static struct kretprobe alloc_pages_nodemask_kretprobe = {
    .handler        = alloc_pages_nodemask_ret_handler,
    .entry_handler    = alloc_pages_nodemask_entry_handler,
    .data_size        = sizeof(struct alloc_pages_nodemask_data),
    /* Probe up to MAXACTIVE instances concurrently. */
    .maxactive        = (int)MAXACTIVE,
    .kp.symbol_name = "__alloc_pages_nodemask",
};

int register_alloc_pages_nodemask(void)
{
    int ret;
    ret = register_kretprobe(&alloc_pages_nodemask_kretprobe);
    if (ret < 0)
    {
        printk(KERN_INFO "%s failed, returned %d\n",
                __func__, ret);
        return -1;
    }
    printk(KERN_INFO "Planted return probe at %s: %p\n",
            alloc_pages_nodemask_kretprobe.kp.symbol_name, 
            alloc_pages_nodemask_kretprobe.kp.addr);
    return 0;
}

void unregister_alloc_pages_nodemask(void)
{
    unregister_kretprobe(&alloc_pages_nodemask_kretprobe);
    printk(KERN_INFO "%s\n", __func__);
}

static void jfree_pages_ok(struct page *page, unsigned int order)
{
    log_page_free(page, order);

    /* Always end with a call to jprobe_return(). */
    jprobe_return();
}

static struct jprobe free_pages_ok_jprobe = {
    .entry            = jfree_pages_ok,
    .kp.symbol_name    = "__free_pages_ok",
};

int register_free_pages_ok(void)
{
    int ret;
    ret = register_jprobe(&free_pages_ok_jprobe);
    if (ret < 0) {
        printk(KERN_INFO "%s failed, returned %d\n",
                __func__, ret);
        return -1;
    }
    printk(KERN_INFO "Planted jprobe at %s: %p\n",
            free_pages_ok_jprobe.kp.symbol_name, 
            free_pages_ok_jprobe.kp.addr);
    return 0;
}

void unregister_free_pages_ok(void)
{
    unregister_jprobe(&free_pages_ok_jprobe);
    printk(KERN_INFO "%s\n", __func__);
}

static void jfree_hot_cold_page(struct page *page, unsigned int order)
{
    log_page_free(page, order);

    /* Always end with a call to jprobe_return(). */
    jprobe_return();
}

static struct jprobe free_hot_cold_page_jprobe = {
    .entry            = jfree_hot_cold_page,
    .kp.symbol_name    = "free_hot_cold_page",
};

int register_free_hot_cold_page(void)
{
    int ret;
    ret = register_jprobe(&free_hot_cold_page_jprobe);
    if (ret < 0) {
        printk(KERN_INFO "%s failed, returned %d\n",
                __func__, ret);
        return -1;
    }
    printk(KERN_INFO "Planted jprobe at %s: %p\n",
            free_hot_cold_page_jprobe.kp.symbol_name, 
            free_hot_cold_page_jprobe.kp.addr);
    return 0;
}

void unregister_free_hot_cold_page(void)
{
    unregister_jprobe(&free_hot_cold_page_jprobe);
    printk(KERN_INFO "%s\n", __func__);
}

/*
 * 注意顺序，出错时按逆序unregister
 */
int register_probes(void)
{
    int ret = 0;

    ret = register_free_pages_ok();
    if(ret)
    {
        return ret;
    }

    ret = register_free_hot_cold_page();
    if(ret)
    {
        unregister_free_pages_ok();
        return ret;
    }

    /*
     * 分配函数的注册放到最后，是因为如果放到前面的话，
     * 可能我们的释放函数还没注册就有内存块被分配和释放了，
     * 这样就有内存块实际被释放了，但是我们认为没有释放。
     */
    ret = register_alloc_pages_nodemask();
    if(ret)
    {
        unregister_free_hot_cold_page();
        unregister_free_pages_ok();
        return ret;
    }

    return 0;
}

void unregister_probes(void)
{
    unregister_alloc_pages_nodemask();
    unregister_free_hot_cold_page();
    unregister_free_pages_ok();
}

int huawei_log_page_init(void)
{
    int ret;
	struct proc_dir_entry *entry;
	entry = create_proc_entry(PROC_NAME,0,NULL);
	if(entry)
	  entry->proc_fops = &proc_file_ops;

	printk("totalram_pages = %lu\n",totalram_pages);
    ret = init_lp_pool();
    if(ret)
        return ret;

    ret = init_st_pool();
    if(ret)
    {
        exit_lp_pool();
        return ret;
    }

    ret = register_probes();
    if(ret)
    {
        exit_st_pool();
        exit_lp_pool();
        return ret;
    }
    return 0;
}

void huawei_log_page_exit(void)
{
	remove_proc_entry(PROC_NAME,NULL);
    unregister_probes();
    exit_st_pool();
    exit_lp_pool();

    printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_log_page_init);
module_exit(huawei_log_page_exit);
MODULE_LICENSE("GPL");
