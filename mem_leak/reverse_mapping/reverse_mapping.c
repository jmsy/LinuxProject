#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rmap.h>
#include <linux/list.h>
#include <linux/prio_tree.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

struct anon_vma *(*page_lock_anon_vma_own)(struct page *)=(struct anon_vma *(*)(struct page *))0xffffffff811163b0;
void (*page_unlock_anon_vma_own)(struct anon_vma *)=(void (*)(struct anon_vma *))0xffffffff811166d0;
struct vm_area_struct *(*vma_prio_tree_next_own)(struct vm_area_struct *,struct prio_tree_iter *)=\
(struct vm_area_struct *(*)(struct vm_area_struct *,struct prio_tree_iter *))0xffffffff811019b0;

static void *handler_seq_start(struct seq_file *s,loff_t *pos)
{
	if((int)*pos <= totalram_pages)
		return pos;
	else
		return NULL;
}

static void *handler_seq_next(struct seq_file *s,void *v,loff_t *pos)
{
	(*pos)++;
	if(*pos <= totalram_pages)
		return pos;
	else
		return NULL;
}
static void handler_seq_stop(struct seq_file *s,void *v)
{
}
static int handler_seq_show(struct seq_file *s,void *v)
{
	struct page *valid_page;
	struct vm_area_struct *vma;
	struct task_struct *page_owner;
	struct anon_vma *av;
	struct prio_tree_iter iter;
	struct address_space *mapping;
	long *pfn;
	pgoff_t pgoff;
	pfn=(long *)v;

	if(!pfn_valid(*pfn))
		return 0;
	//convert physical address to page
	valid_page=pfn_to_page(*pfn);
	if(valid_page->mapping==NULL)
		seq_printf(s,"swap cache pfn:%ld\n",*pfn);
	else if(PageAnon(valid_page)){
		seq_printf(s,"anonymous pfn:%ld ",*pfn);
		av=page_lock_anon_vma_own(valid_page);
		if(!av)
			return 0;
		list_for_each_entry(vma, &(av->head), anon_vma_node){
			page_owner=vma->vm_mm->owner;
			seq_printf(s,"owner:%d %s\n",page_owner->pid,page_owner->comm);
		}
		page_unlock_anon_vma_own(av);
	}
	else{
		pgoff=valid_page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
		mapping=valid_page->mapping;
		seq_printf(s,"mapped pfn:%ld\n",*pfn);
		for(prio_tree_iter_init(&iter,&mapping->i_mmap,pgoff,pgoff),vma=NULL;\
				(vma=vma_prio_tree_next_own(vma,&iter));){
	//	vma_prio_tree_foreach(vma,&iter,&mapping->i_mmap,pgoff,pgoff){
			page_owner=vma->vm_mm->owner;
			seq_printf(s,"owner:%d %s\n",page_owner->pid,page_owner->comm);
		}
	}
	return 0;
}
static struct seq_operations handler_seq_ops={
	.start = handler_seq_start,
	.next = handler_seq_next,
	.stop = handler_seq_stop,
	.show = handler_seq_show
};
static int handler_open(struct inode *inode,struct file *file)
{
        return seq_open(file,&handler_seq_ops);
};
static struct file_operations handler_proc_ops={
        .owner = THIS_MODULE,
        .open = handler_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = seq_release
};


static int init_rmap(void)
{
	struct proc_dir_entry *entry;
        entry = create_proc_entry("rmap",0,NULL);
	if(entry){
		entry->proc_fops = &handler_proc_ops;
	}
	return 0;
}
static void exit_rmap(void)
{
	remove_proc_entry("rmap",NULL);
}

MODULE_LICENSE("GPL");
module_init(init_rmap);
module_exit(exit_rmap);
