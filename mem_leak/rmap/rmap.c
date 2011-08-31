#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/rmap.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/memcontrol.h>
#include <asm/page.h>
#include <linux/mm_types.h>
#include <linux/prio_tree.h>
#include <linux/prefetch.h>

/*以下三个宏定义是内核为了实现优先搜索树而定义的*/
#define RADIX_INDEX_USER(vma)  ((vma)->vm_pgoff)
#define VMA_SIZE_USER(vma)	  (((vma)->vm_end - (vma)->vm_start) >> PAGE_SHIFT)
#define HEAP_INDEX_USER(vma)	  ((vma)->vm_pgoff + (VMA_SIZE_USER(vma) - 1))

/*这个宏记录与一块内存相关的最多的进程数，当前代码
 *还没有使用 */
#define VM_NUM	20   

/*使用此数组记录相关进程的pid，保证打印时不会出现
 *重复信息（对应多个vma属于同一个进程的情况）*/
int pid_arr[VM_NUM];

/*将获得的与固定内存单元相关的进程
 *task_struct链接在一个双向链表中
 */
struct list_head task_head;
LIST_HEAD(task_head);

/*获得相关进程信息存储在此结构中*/
struct task_info
{
	struct list_head list;
	int pid;
	char *taskname; //进程名字
};

/*对应内核get_index()函数（优先搜索树）*/
static void get_index_user(const struct prio_tree_root *root,
    const struct prio_tree_node *node,
    unsigned long *radix, unsigned long *heap)
{
	if (root->raw) {
		struct vm_area_struct *vma = prio_tree_entry(
		    node, struct vm_area_struct, shared.prio_tree_node);

		*radix = RADIX_INDEX_USER(vma);
		*heap = HEAP_INDEX_USER(vma);
	}
	else {
		*radix = node->start;
		*heap = node->last;
	}
}

/*对应内核prio_tree_node()函数（优先搜索树）*/
static struct prio_tree_node *prio_tree_left_user(struct prio_tree_iter *iter,
		unsigned long *r_index, unsigned long *h_index)
{
	if (prio_tree_left_empty(iter->cur))
		return NULL;

	get_index_user(iter->root, iter->cur->left, r_index, h_index);

	if (iter->r_index <= *h_index) {
		iter->cur = iter->cur->left;
		iter->mask >>= 1;
		if (iter->mask) {
			if (iter->size_level)
				iter->size_level++;
		} else {
			if (iter->size_level) {
				BUG_ON(!prio_tree_left_empty(iter->cur));
				BUG_ON(!prio_tree_right_empty(iter->cur));
				iter->size_level++;
				iter->mask = ULONG_MAX;
			} else {
				iter->size_level = 1;
				iter->mask = 1UL << (BITS_PER_LONG - 1);
			}
		}
		return iter->cur;
	}

	return NULL;
}

/*同上（优先搜索树）*/
static struct prio_tree_node *prio_tree_right_user(struct prio_tree_iter *iter,
		unsigned long *r_index, unsigned long *h_index)
{
	unsigned long value;

	if (prio_tree_right_empty(iter->cur))
		return NULL;

	if (iter->size_level)
		value = iter->value;
	else
		value = iter->value | iter->mask;

	if (iter->h_index < value)
		return NULL;

	get_index_user(iter->root, iter->cur->right, r_index, h_index);

	if (iter->r_index <= *h_index) {
		iter->cur = iter->cur->right;
		iter->mask >>= 1;
		iter->value = value;
		if (iter->mask) {
			if (iter->size_level)
				iter->size_level++;
		} else {
			if (iter->size_level) {
				BUG_ON(!prio_tree_left_empty(iter->cur));
				BUG_ON(!prio_tree_right_empty(iter->cur));
				iter->size_level++;
				iter->mask = ULONG_MAX;
			} else {
				iter->size_level = 1;
				iter->mask = 1UL << (BITS_PER_LONG - 1);
			}
		}
		return iter->cur;
	}

	return NULL;
}

static struct prio_tree_node *prio_tree_parent_user(struct prio_tree_iter *iter)
{
	iter->cur = iter->cur->parent;
	if (iter->mask == ULONG_MAX)
		iter->mask = 1UL;
	else if (iter->size_level == 1)
		iter->mask = 1UL;
	else
		iter->mask <<= 1;
	if (iter->size_level)
		iter->size_level--;
	if (!iter->size_level && (iter->value & iter->mask))
		iter->value ^= iter->mask;
	return iter->cur;
}

static inline int overlap_user(struct prio_tree_iter *iter,
		unsigned long r_index, unsigned long h_index)
{
	return iter->h_index >= r_index && iter->r_index <= h_index;
}



static struct prio_tree_node *prio_tree_first_user(struct prio_tree_iter *iter)
{
	struct prio_tree_root *root;
	unsigned long r_index, h_index;

	INIT_PRIO_TREE_ITER(iter);

	root = iter->root;
	if (prio_tree_empty(root))
		return NULL;

	get_index_user(root, root->prio_tree_node, &r_index, &h_index);

	if (iter->r_index > h_index)
		return NULL;

	iter->mask = 1UL << (root->index_bits - 1);
	iter->cur = root->prio_tree_node;

	while (1) {
		if (overlap_user(iter, r_index, h_index))
			return iter->cur;

		if (prio_tree_left_user(iter, &r_index, &h_index))
			continue;

		if (prio_tree_right_user(iter, &r_index, &h_index))
			continue;

		break;
	}
	return NULL;
}

struct prio_tree_node *prio_tree_next_user(struct prio_tree_iter *iter)
{
	unsigned long r_index, h_index;

	if (iter->cur == NULL)
		return prio_tree_first_user(iter);

repeat:
	while (prio_tree_left_user(iter, &r_index, &h_index))
		if (overlap_user(iter, r_index, h_index))
			return iter->cur;

	while (!prio_tree_right_user(iter, &r_index, &h_index)) {
	    	while (!prio_tree_root(iter->cur) &&
				iter->cur->parent->right == iter->cur)
			prio_tree_parent_user(iter);

		if (prio_tree_root(iter->cur))
			return NULL;

		prio_tree_parent_user(iter);
	}

	if (overlap_user(iter, r_index, h_index))
		return iter->cur;

	goto repeat;
}


static inline void prio_tree_iter_init_user(struct prio_tree_iter *iter,
		struct prio_tree_root *root, pgoff_t r_index, pgoff_t h_index)
{
	iter->root = root;
	iter->r_index = r_index;
	iter->h_index = h_index;
	iter->cur = NULL;
}

struct vm_area_struct *vma_prio_tree_next_user(struct vm_area_struct *vma,
					struct prio_tree_iter *iter)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *next;

	if (!vma) {
		/*
		 * First call is with NULL vma
		 */
		ptr = prio_tree_next_user(iter);
		if (ptr) {
			next = prio_tree_entry(ptr, struct vm_area_struct,
						shared.prio_tree_node);
			prefetch(next->shared.vm_set.head);
			return next;
		} else
			return NULL;
	}

	if (vma->shared.vm_set.parent) {
		if (vma->shared.vm_set.head) {
			next = vma->shared.vm_set.head;
			prefetch(next->shared.vm_set.list.next);
			return next;
		}
	} else {
		next = list_entry(vma->shared.vm_set.list.next,
				struct vm_area_struct, shared.vm_set.list);
		if (!next->shared.vm_set.head) {
			prefetch(next->shared.vm_set.list.next);
			return next;
		}
	}

	ptr = prio_tree_next_user(iter);
	if (ptr) {
		next = prio_tree_entry(ptr, struct vm_area_struct,
					shared.prio_tree_node);
		prefetch(next->shared.vm_set.head);
		return next;
	} else
		return NULL;
}

/*内核实现此宏的过程中使用了对模块不可用的函数
 *故在此重写该宏（遍历优先搜索树）*/
#define vma_prio_tree_foreach_user(vma, iter, root, begin, end)	\
	for (prio_tree_iter_init(iter, root, begin, end), vma = NULL;	\
		(vma = vma_prio_tree_next_user(vma, iter)); )


/*以下三个函数作用是避免记录重复的进程信息（未使用）*/
void init_pid_arr(void)
{
	int i;
	for(i=0;i<VM_NUM;i++)
		pid_arr[i]=-1;
}

int check_exist(int id)
{
	int i=0;
	while(i<VM_NUM&&pid_arr[i]!=-1&&pid_arr[i]!=id)
		i++;
	if(i<VM_NUM)
		return 1;
	else
		return 0;
}

int insert_arr(int id)
{
	int i=0;
	while(pid_arr[i]!=-1&&i<VM_NUM)
		i++;
	if(i==VM_NUM)
	{
		printk("lack of pid_arr!\n");
		return 0;
	}
	else
	{
		pid_arr[i]=id;
		return 1;
	}
}

/*由page结构获取与其相关的vma表头（匿名页）*/
struct anon_vma *page_lock_anon_vma_user(struct page *page)
{
	struct anon_vma *anon_vma;
	unsigned long anon_mapping;

	rcu_read_lock();
	anon_mapping = (unsigned long) page->mapping;
	if (!(anon_mapping & PAGE_MAPPING_ANON))
		goto out;
	if (!page_mapped(page))
		goto out;

	anon_vma = (struct anon_vma *) (anon_mapping - PAGE_MAPPING_ANON);
	spin_lock(&anon_vma->lock);
	return anon_vma;
out:
	rcu_read_unlock();
	return NULL;
}

/*释放获取vma表头过程中申请的锁，与page_lock_anon_vma_user对应*/
void page_unlock_anon_vma_user(struct anon_vma *anon_vma)
{
	spin_unlock(&anon_vma->lock);
	rcu_read_unlock();
}


/*给定一个页面，找出与此相关的进程信息（pid，name）*/
static void page_locate_anon(struct page *page)
{
	struct anon_vma *anon_vma;
	struct vm_area_struct *vma;
	struct list_head *pos;
	struct task_info *temp;
	struct mm_struct *mm;
	struct task_info *new_task;

	printk("--------the page is based on anon!--------\n");

	anon_vma = page_lock_anon_vma_user(page);
	if (!anon_vma)
		return;
	list_for_each_entry(vma, &anon_vma->head, anon_vma_node) {

		mm=vma->vm_mm;
		new_task = (struct task_info *)vmalloc(sizeof(struct task_info));
		new_task->pid = mm->owner->pid;
		new_task->taskname = mm->owner->comm;
		list_add(&new_task->list,&task_head);
		new_task=NULL;

	}

	page_unlock_anon_vma_user(anon_vma);
	init_pid_arr();   //intit the pid_arr array
	printk("the related task's infomation is:\n");
	list_for_each(pos,&task_head)
	{
		temp = list_entry(pos,struct task_info,list);
		printk("pid=%d  process=%s\n",temp->pid,temp->taskname);
		vfree(temp);
		temp=NULL;
	}
	init_pid_arr();  //fu wei
}

EXPORT_SYMBOL(page_locate_anon);

static void page_locate_file(struct page *page)
{
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;
	struct list_head *pos;
	struct task_info *temp;
	struct mm_struct *mm;
	struct task_info *new_task;

	printk("--------the page is based on file!--------\n");

	vma_prio_tree_foreach_user(vma, &iter, &mapping->i_mmap, pgoff, pgoff) {
		/*
		 * If we are reclaiming on behalf of a cgroup, skip
		 * counting on behalf of references from different
		 * cgroups
		 */
		mm=vma->vm_mm;
		new_task = (struct task_info *)vmalloc(sizeof(struct task_info));
		new_task->pid = mm->owner->pid;
		new_task->taskname = mm->owner->comm;
		list_add(&new_task->list,&task_head);
		new_task=NULL;

	}


	printk("the related task's infomation is:\n");
	list_for_each(pos,&task_head)
	{
		temp = list_entry(pos,struct task_info,list);
		printk("pid=%d  process=%s\n",temp->pid,temp->taskname);
		vfree(temp);
		temp=NULL;
	}
	init_pid_arr();  //fu wei

}


void get_process_info(unsigned long paddr)
{
	struct page *p = pfn_to_page(paddr>>PAGE_SHIFT);
	unsigned long mapping;
	if(p->mapping==NULL)
		printk("--------the page will be swapped!--------\n");
	else
	{
		mapping = (unsigned long)p->mapping;
		if(PageAnon(p))
			page_locate_anon(p);
		else
			page_locate_file(p);
	}

}

EXPORT_SYMBOL(get_process_info);
static int rmap_init(void)
{
//	printk("rmap module has been installed!\n");
	return 0;
}

static void rmap_exit(void)
{
//	printk("rmap module has been uninstalled!\n");
}

MODULE_LICENSE("GPL");
module_init(rmap_init);
module_exit(rmap_exit);

