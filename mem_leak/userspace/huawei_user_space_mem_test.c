#include<linux/module.h>
#include<linux/kernel.h>
#include<asm/pgtable.h>
#include<linux/sched.h>
#include<linux/mm.h>
#include<linux/hugetlb.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>

#include"huawei_user_space_mem.h"

#define PID 1587
#define LIBC_ADDR   (0x7fe2e3bf5000)

unsigned long get_paddr(struct task_struct *p, unsigned long vaddr)
{
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    struct mm_struct *mm;
    struct vm_area_struct *vma;

    mm = p->mm;
    vma = find_vma(mm, vaddr);

    if(unlikely(is_vm_hugetlb_page(vma)))
    {
        printk("vaddr[%p]:hugetlb page\n", (void*)vaddr);
        return 0;
    }

    pgd = pgd_offset(mm, vaddr);
    pud = pud_offset(pgd, vaddr);
    pmd = pmd_offset(pud, vaddr);
    pte = pte_offset_kernel(pmd, vaddr);
//pte_pfn:由页表项得到物理页框号
    return (unsigned long)(pte_pfn(*pte) << PAGE_SHIFT);
}

unsigned long vaddr_to_paddr(pid_t pid, unsigned long vaddr)
{
    unsigned long paddr = 0;
    struct task_struct *p;
    struct list_head *_p, *_n;

    list_for_each_safe(_p, _n, &current->tasks) {
        p = list_entry(_p, struct task_struct, tasks);

        if(p->pid == pid)
        {
            paddr = get_paddr(p, vaddr);
            break;
        }
    }

    return paddr;
}

int huawei_user_space_mem_test_init(void)
{
    unsigned long paddr;
	ktime_t start;
	ktime_t end;
	s64 delta;

	printk(KERN_INFO "------------Hi all, %s\n", __func__);

    paddr = vaddr_to_paddr((pid_t)PID, (unsigned long)LIBC_ADDR);
    if(paddr == 0)
    {
        printk("vaddr_to_paddr(%p) = %lu\n", (void*)(unsigned long)LIBC_ADDR, paddr);
		printk("this physic memory was swapped!\n");
        return 1;
    }

	start = ktime_get();

    print_vaddr_of_paddr(paddr);

	end = ktime_get();
	delta = ktime_to_ns(ktime_sub(end, start));
	printk(KERN_INFO "%s and took %lld ns to execute\n",
			"print_vaddr_of_paddr", (long long)delta);
    
	return 0;
}

void huawei_user_space_mem_test_exit(void)
{
	printk(KERN_INFO "------------Goodbye %s\n", __func__);
}

module_init(huawei_user_space_mem_test_init);
module_exit(huawei_user_space_mem_test_exit);
MODULE_LICENSE("GPL");
